/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Written (W) 2010 Christian Widmer
 * Copyright (C) 2010 Max-Planck-Society
 */

#ifndef _MULTITASKKERNELTREENORMALIZER_H___
#define _MULTITASKKERNELTREENORMALIZER_H___

#include "kernel/KernelNormalizer.h"
#include "kernel/Kernel.h"
#include <algorithm>
#include <map>
#include <set>

namespace shogun
{


class CNode: public CSGObject
{

public:


	float64_t beta;

	CNode() {
		parent = NULL;
		beta = 1.0;
	}

    /**
    fetch all ancesters of current node (excluding self)
    until root is reached including root

    @return: list of nodes on the path to root
    @rtype: list<TreeNode>
    **/
    std::set<CNode*> get_path_root() {

        std::set<CNode*> nodes_on_path = std::set<CNode*>();

        CNode* node = this;

        while (node != NULL) {
            nodes_on_path.insert(node);
            node = node->parent;
        }

        return nodes_on_path;
    }



    /**
    add node as child of current leaf

    @param node: child node
    @type node: TreeNode
    **/
	void add_child(CNode* node)
	{
        node->parent = this;
        this->children.push_back(node);
	}


	/** @return object name */
	inline virtual const char* get_name() const
	{
		return "CNode";
	}

	/** @return boolen to indicate, whether node has children */
	bool is_leaf()
	{
		return children.empty();

	}

protected:

	CNode* parent;
	std::vector<CNode*> children;

};


class CTaxonomy : public CSGObject
{

public:


	CTaxonomy(){
		root = new CNode();
		nodes.push_back(root);

		name2id = std::map<std::string, int32_t>();
		name2id["root"] = 0;
	}


	CNode* get_node(int32_t task_id) {
		return nodes[task_id];
	}

	void set_root_beta(float64_t beta)
	{
		nodes[0]->beta = beta;
	}

	CNode* add_node(std::string parent_name, std::string child_name, float64_t beta) {


		if (child_name=="")	SG_ERROR("child_name empty");
		if (parent_name=="") SG_ERROR("parent_name empty");


		CNode* child_node = new CNode();

		child_node->beta = beta;

		nodes.push_back(child_node);
		int32_t id = nodes.size()-1;

		name2id[child_name] = id;

		//create edge
		CNode* parent = nodes[name2id[parent_name]];

		parent->add_child(child_node);

		return child_node;

	}

	int32_t get_id(std::string name) {
		//std::cout << "-->" << name << "<--" << std::endl;
		return name2id[name];
	}

	/**
	 * @param task_lhs task_id on left hand side
	 * @param task_rhs task_id on right hand side
	 * @return similarity between tasks
	 */
	float64_t compute_node_similarity(int32_t task_lhs, int32_t task_rhs)
	{

		CNode* node_lhs = get_node(task_lhs);
		CNode* node_rhs = get_node(task_rhs);

		std::set<CNode*> root_path_lhs = node_lhs->get_path_root();
		std::set<CNode*> root_path_rhs = node_rhs->get_path_root();

		std::set<CNode*> intersection;

		std::set_intersection(root_path_lhs.begin(), root_path_lhs.end(),
							  root_path_rhs.begin(), root_path_rhs.end(),
							  std::inserter(intersection, intersection.end()));

		// sum up weights
		float64_t gamma = 0;
		for (std::set<CNode*>::const_iterator p = intersection.begin(); p != intersection.end(); ++p) {
			gamma += (*p)->beta;
		}

		return gamma;

	}


	int32_t get_num_nodes()
	{
		return (int32_t)(nodes.size());
	}

	int32_t get_num_leaves()
	{
		int32_t num_leaves = 0;

		for (int32_t i=0; i!=get_num_nodes(); i++)
		{
			if (get_node(i)->is_leaf()==true)
			{
				num_leaves++;
			}
		}

		return num_leaves;
	}

	float64_t get_node_weight(int32_t idx)
	{
		CNode* node = get_node(idx);
		return node->beta;
	}

	void set_node_weight(int32_t idx, float64_t weight)
	{
		CNode* node = get_node(idx);
		node->beta = weight;
	}

	/** @return object name */
	inline virtual const char* get_name() const
	{
		return "CTaxonomy";
	}


	std::map<std::string, int32_t> get_name2id() {
		return name2id;
	}


protected:

	CNode* root;
	std::map<std::string, int32_t> name2id;
	std::vector<CNode*> nodes;

};





/** @brief The MultitaskKernel allows Multitask Learning via a modified kernel function.
 *
 * This effectively normalizes the vectors in feature space to norm 1 (see
 * CSqrtDiagKernelNormalizer)
 *
 * \f[
 * k'({\bf x},{\bf x'}) = ...
 * \f]
 */
class CMultitaskKernelTreeNormalizer: public CKernelNormalizer
{



public:

	/** default constructor
	 */
	CMultitaskKernelTreeNormalizer() : scale(1.0)
	{
	}

	/** default constructor
	 *
	 * @param task_lhs task vector with containing task_id for each example for left hand side
	 * @param task_rhs task vector with containing task_id for each example for right hand side
	 */
	CMultitaskKernelTreeNormalizer(std::vector<std::string> task_lhs,
								   std::vector<std::string> task_rhs,
								   CTaxonomy tax) : scale(1.0)
	{

		taxonomy = tax;
		set_task_vector_lhs(task_lhs);
		set_task_vector_rhs(task_rhs);

		num_nodes = taxonomy.get_num_nodes();

		std::cout << "num nodes:" << num_nodes << std::endl;

		dependency_matrix = std::vector<float64_t>(num_nodes * num_nodes);

		update_cache();
	}


	/** default destructor */
	virtual ~CMultitaskKernelTreeNormalizer()
	{
	}

	/** initialization of the normalizer
	 * @param k kernel */
	virtual bool init(CKernel* k)
	{
		ASSERT(k);
		int32_t num_lhs = k->get_num_vec_lhs();
		int32_t num_rhs = k->get_num_vec_rhs();
		ASSERT(num_lhs>0);
		ASSERT(num_rhs>0);


		//same as first-element normalizer
		CFeatures* old_lhs=k->lhs;
		CFeatures* old_rhs=k->rhs;
		k->lhs=old_lhs;
		k->rhs=old_lhs;

		scale=k->compute(0, 0);

		k->lhs=old_lhs;
		k->rhs=old_rhs;


		return true;
	}

	/** update cache */
	void update_cache()
	{
		for (int32_t i=0; i!=num_nodes; i++)
		{
			for (int32_t j=0; j!=num_nodes; j++)
			{

				float64_t similarity = taxonomy.compute_node_similarity(i, j);
				set_node_similarity(i,j,similarity);

			}

		}
	}



	/** normalize the kernel value
	 * @param value kernel value
	 * @param idx_lhs index of left hand side vector
	 * @param idx_rhs index of right hand side vector
	 */
	inline virtual float64_t normalize(float64_t value, int32_t idx_lhs, int32_t idx_rhs)
	{

		//lookup tasks
		int32_t task_idx_lhs = task_vector_lhs[idx_lhs];
		int32_t task_idx_rhs = task_vector_rhs[idx_rhs];

		//std::cout << task_idx_lhs << ", " << task_idx_rhs << std::endl;

		//lookup similarity
		float64_t task_similarity = get_node_similarity(task_idx_lhs, task_idx_rhs);
		//float64_t task_similarity = taxonomy.compute_node_similarity(task_idx_lhs, task_idx_rhs);

		//take task similarity into account
		float64_t similarity = (value/scale) * task_similarity;


		return similarity;

	}

	/** normalize only the left hand side vector
	 * @param value value of a component of the left hand side feature vector
	 * @param idx_lhs index of left hand side vector
	 */
	inline virtual float64_t normalize_lhs(float64_t value, int32_t idx_lhs)
	{
		SG_ERROR("normalize_lhs not implemented");
		return 0;
	}

	/** normalize only the right hand side vector
	 * @param value value of a component of the right hand side feature vector
	 * @param idx_rhs index of right hand side vector
	 */
	inline virtual float64_t normalize_rhs(float64_t value, int32_t idx_rhs)
	{
		SG_ERROR("normalize_rhs not implemented");
		return 0;
	}

	/** @return vec task vector with containing task_id for each example on left hand side */
	/*
	std::vector<std::string> get_task_vector_lhs() const
	{
		return task_vector_lhs;
	}
	*/

	/** @param vec task vector with containing task_id for each example */
	void set_task_vector_lhs(std::vector<std::string> vec)
	{

		task_vector_lhs.clear();

		for (int32_t i = 0; i != (int32_t)(vec.size()); ++i)
		{
			task_vector_lhs.push_back(taxonomy.get_id(vec[i]));
		}

	}

	/** @return vec task vector with containing task_id for each example on right hand side */
	/*
	std::vector<std::string> get_task_vector_rhs() const
	{
		return task_vector_rhs;
	}
	*/

	/** @param vec task vector with containing task_id for each example */
	void set_task_vector_rhs(std::vector<std::string> vec)
	{


		std::cout << "map size:" << taxonomy.get_name2id().size() << std::endl;

		/*

	    // Iterate over the map and print out all key/value pairs.
	    // Using a const_iterator since we are not going to change the values.
	    for(std::map<std::string, int32_t>::const_iterator it = taxonomy.get_name2id().begin(); it != taxonomy.get_name2id().end(); ++it)
	    {
	        std::cout << "Who(key = first): " << it->first;
	        std::cout << " Score(value = second): " << it->second << std::endl;
	        std::cout.flush();
	    }
		*/

		task_vector_rhs.clear();

		for (int32_t i = 0; i != (int32_t)(vec.size()); ++i)
		{
			task_vector_rhs.push_back(taxonomy.get_id(vec[i]));
		}

	}

	/** @param vec task vector with containing task_id for each example */
	void set_task_vector(std::vector<std::string> vec)
	{
		set_task_vector_lhs(vec);
		set_task_vector_rhs(vec);
	}

	int32_t get_num_nodes()
	{

		return taxonomy.get_num_nodes();

	}

	float64_t get_node_weight(int32_t idx)
	{

		return taxonomy.get_node_weight(idx);

	}

	void set_node_weight(int32_t idx, float64_t weight)
	{

		taxonomy.set_node_weight(idx, weight);

		update_cache();

	}



	/**
	 * @param node_lhs node_id on left hand side
	 * @param node_rhs node_id on right hand side
	 * @return similarity between nodes
	 */
	float64_t get_node_similarity(int32_t node_lhs, int32_t node_rhs)
	{

		ASSERT(node_lhs < num_nodes && node_lhs >= 0);
		ASSERT(node_rhs < num_nodes && node_rhs >= 0);

		return dependency_matrix[node_lhs * num_nodes + node_rhs];

	}

	/**
	 * @param node_lhs node_id on left hand side
	 * @param node_rhs node_id on right hand side
	 * @param similarity similarity between nodes
	 */
	void set_node_similarity(int32_t node_lhs, int32_t node_rhs,
			float64_t similarity)
	{

		ASSERT(node_lhs < num_nodes && node_lhs >= 0);
		ASSERT(node_rhs < num_nodes && node_rhs >= 0);

		dependency_matrix[node_lhs * num_nodes + node_rhs] = similarity;

	}


	/** @return object name */
	inline virtual const char* get_name() const
	{
		return "MultitaskKernelTreeNormalizer";
	}



protected:


	void init_dependency_matrix()
	{

		int32_t i = 0;

	}





	/** taxonomy **/
	CTaxonomy taxonomy;

	/** number of tasks **/
	int32_t num_nodes;

	/** task vector indicating to which task each example on the left hand side belongs **/
	std::vector<int32_t> task_vector_lhs;

	/** task vector indicating to which task each example on the right hand side belongs **/
	std::vector<int32_t> task_vector_rhs;

	/** value of first element **/
	float64_t scale;

	/** MxM matrix encoding similarity between tasks **/
	std::vector<float64_t> dependency_matrix;

};
}
#endif
