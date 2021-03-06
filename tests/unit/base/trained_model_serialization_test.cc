#include "environments/LinearTestEnvironment.h"
#include "environments/MultiLabelTestEnvironment.h"
#include "environments/RegressionTestEnvironment.h"
#include "utils/Utils.h"
#include <gtest/gtest.h>
#include <shogun/base/some.h>
#include <shogun/features/DenseFeatures.h>
#include <shogun/io/CSVFile.h>
#include <shogun/io/SGIO.h>
#include <shogun/io/SerializableAsciiFile.h>
#include <shogun/io/SerializableHdf5File.h>
#include <shogun/kernel/GaussianKernel.h>
#include <shogun/labels/BinaryLabels.h>
#include <shogun/labels/Labels.h>
#include <shogun/labels/RegressionLabels.h>
#include <shogun/machine/Machine.h>

using namespace shogun;

extern LinearTestEnvironment* linear_test_env;
extern MultiLabelTestEnvironment* multilabel_test_env;
extern RegressionTestEnvironment* regression_test_env;

template <class T>
class TrainedModelSerializationFixture : public ::testing::Test
{
protected:
	void SetUp()
	{
		machine = new T();
		SG_REF(machine)

		deserialized_machine = new T();
		SG_REF(deserialized_machine)

		this->load_data(this->machine->get_machine_problem_type());
	}

	void TearDown()
	{
		SG_UNREF(train_feats)
		SG_UNREF(test_feats)
		SG_UNREF(train_labels)
		SG_UNREF(machine)
		SG_UNREF(deserialized_machine)
	}

	void load_data(EProblemType pt)
	{
		switch (pt)
		{
		case PT_BINARY:
		case PT_CLASS:
		{
			std::shared_ptr<GaussianCheckerboard> mock_data =
			    linear_test_env->getBinaryLabelData();
			train_feats = mock_data->get_features_train();
			test_feats = mock_data->get_features_test();
			train_labels = mock_data->get_labels_train();
			break;
		}

		case PT_MULTICLASS:
		{
			std::shared_ptr<GaussianCheckerboard> mock_data =
			    multilabel_test_env->getMulticlassFixture();
			train_feats = mock_data->get_features_train();
			test_feats = mock_data->get_features_test();
			train_labels = mock_data->get_labels_train();
			break;
		}

		case PT_REGRESSION:
			train_feats = regression_test_env->get_features_train();
			test_feats = regression_test_env->get_features_test();
			train_labels = regression_test_env->get_labels_train();
			break;

		default:
			SG_SERROR("Unsupported problem type: %d\n", pt);
			FAIL();
		}

		SG_REF(train_feats)
		SG_REF(test_feats)
		SG_REF(train_labels)
	}

	bool serialize_machine(
	    CMachine* cmachine, std::string& filename,
	    bool store_model_features = false)
	{
		std::string class_name = cmachine->get_name();
		filename = "shogun-unittest-trained-model-serialization-" + class_name +
		           ".XXXXXX";
		generate_temp_filename(const_cast<char*>(filename.c_str()));

		CSerializableHdf5File* file =
		    new CSerializableHdf5File(filename.c_str(), 'w');
		cmachine->set_store_model_features(store_model_features);
		bool save_success = cmachine->save_serializable(file);
		file->close();
		SG_FREE(file);

		return save_success;
	}

	bool deserialize_machine(CMachine* cmachine, std::string filename)
	{
		CSerializableHdf5File* file =
		    new CSerializableHdf5File(filename.c_str(), 'r');
		bool load_success = cmachine->load_serializable(file);

		file->close();
		SG_FREE(file);
		int delete_success = unlink(filename.c_str());

		return load_success && (delete_success == 0);
	}

	CDenseFeatures<float64_t> *train_feats, *test_feats;
	CLabels* train_labels;
	T* machine;
	T* deserialized_machine;
};

#include "trained_model_serialization_test.h"

template <class T>
class TrainedMachineSerialization : public TrainedModelSerializationFixture<T>
{
};

TYPED_TEST_CASE(TrainedMachineSerialization, MachineTypes);

TYPED_TEST(TrainedMachineSerialization, Test)
{
	this->machine->set_labels(this->train_labels);
	this->machine->train(this->train_feats);

	/* to avoid serialization of the data */
	//	machine->set_features(NULL);
	//	machine->set_labels(NULL);

	auto predictions = wrap<CLabels>(this->machine->apply(this->test_feats));

	std::string filename;
	ASSERT_TRUE(this->serialize_machine(this->machine, filename));

	ASSERT_TRUE(
	    this->deserialize_machine(this->deserialized_machine, filename));

	auto deserialized_predictions =
	    wrap<CLabels>(this->deserialized_machine->apply(this->test_feats));

	set_global_fequals_epsilon(1e-7);
	ASSERT(predictions->equals(deserialized_predictions))
	set_global_fequals_epsilon(0);
}

template <class T>
class TrainedKernelMachineSerialization
    : public TrainedModelSerializationFixture<T>
{
};

TYPED_TEST_CASE(TrainedKernelMachineSerialization, KernelMachineTypes);

TYPED_TEST(TrainedKernelMachineSerialization, Test)
{
	CGaussianKernel* kernel = new CGaussianKernel(2.0);
	this->machine->set_kernel(kernel);
	this->machine->set_labels(this->train_labels);

	this->machine->train(this->train_feats);

	auto predictions = wrap<CLabels>(this->machine->apply(this->test_feats));

	for (auto store_model_features : {false, true})
	{
		std::string filename;
		ASSERT_TRUE(
		    this->serialize_machine(
		        this->machine, filename, store_model_features));

		ASSERT_TRUE(
		    this->deserialize_machine(this->deserialized_machine, filename));

		auto deserialized_predictions =
		    wrap<CLabels>(this->deserialized_machine->apply(this->test_feats));

		// allow for lossy serialization format
		set_global_fequals_epsilon(1e-6);
		ASSERT(predictions->equals(deserialized_predictions))
		set_global_fequals_epsilon(0);
	}
}
