#include "Core/Scripting/LanguageDefinitions/Python/ModuleImporters/BitModuleImporter.h"
#include "Core/Scripting/InternalAPIModules/BitAPI.h"
#include "Core/Scripting/HelperClasses/FunctionMetadata.h"

#include "Core/Scripting/LanguageDefinitions/Python/PythonScriptContext.h"
#include <vector>

namespace Scripting::Python::BitModuleImporter
{
static bool initialized_bit_module_importer = false;
static std::string bit_class_name;
static std::vector<FunctionMetadata> all_bit_functions;
static FunctionMetadata* bitwise_and_1_0_metadata = nullptr;
static FunctionMetadata* bitwise_or_1_0_metadata = nullptr;
static FunctionMetadata* bitwise_not_1_0_metadata = nullptr;
static FunctionMetadata* bitwise_xor_1_0_metadata = nullptr;
static FunctionMetadata* logical_and_1_0_metadata = nullptr;
static FunctionMetadata* logical_or_1_0_metadata = nullptr;
static FunctionMetadata* logical_xor_1_0_metadata = nullptr;
static FunctionMetadata* logical_not_1_0_metadata = nullptr;
static FunctionMetadata* bit_shift_left_1_0_metadata = nullptr;
static FunctionMetadata* bit_shift_right_1_0_metadata = nullptr;

void InitFunctionList()
{
  ClassMetadata classMetadata = BitApi::GetAllClassMetadata();
  bit_class_name = classMetadata.class_name;
  all_bit_functions = classMetadata.functions_list;
  int number_of_functions = all_bit_functions.size();
  for (int i = 0; i < number_of_functions; ++i)
  {
    FunctionMetadata* function_reference = &(all_bit_functions[i]);

    switch ((unsigned long long) all_bit_functions[i].function_pointer)
    {
    case (unsigned long long)BitApi::BitwiseAnd:
      bitwise_and_1_0_metadata = function_reference;
      break;
    case (unsigned long long)BitApi::BitwiseOr:
      bitwise_or_1_0_metadata = function_reference;
      break;
    case (unsigned long long)BitApi::BitwiseNot:
      bitwise_not_1_0_metadata = function_reference;
      break;
    case (unsigned long long)BitApi::BitwiseXor:
      bitwise_xor_1_0_metadata = function_reference;
      break;
    case (unsigned long long)BitApi::LogicalAnd:
      logical_and_1_0_metadata = function_reference;
      break;
    case (unsigned long long)BitApi::LogicalOr:
      logical_or_1_0_metadata = function_reference;
      break;
    case (unsigned long long)BitApi::LogicalXor:
      logical_xor_1_0_metadata = function_reference;
      break;
    case (unsigned long long)BitApi::LogicalNot:
      logical_not_1_0_metadata = function_reference;
      break;
    case (unsigned long long)BitApi::BitShiftLeft:
      bit_shift_left_1_0_metadata = function_reference;
      break;
    case (unsigned long long)BitApi::BitShiftRight:
      bit_shift_right_1_0_metadata = function_reference;
      break;
    default:
      throw std::invalid_argument(
          "Unknown argument inside of BitModuleImporter::InitFunctionsList(). Did you add a new "
          "function to the BitApi and forget to update the list in this function?");
      return;
    }
  }
}

static PyObject* python_bitwise_and_1_0(PyObject* self, PyObject* args)
{
  return PythonScriptContext::RunFunction(self, args, bit_class_name, bitwise_and_1_0_metadata);
}

static PyObject* python_bitwise_or_1_0(PyObject* self, PyObject* args)
{
  return PythonScriptContext::RunFunction(self, args, bit_class_name, bitwise_or_1_0_metadata);
}

static PyObject* python_bitwise_not_1_0(PyObject* self, PyObject* args)
{
  return PythonScriptContext::RunFunction(self, args, bit_class_name, bitwise_not_1_0_metadata);
}

static PyObject* python_bitwise_xor_1_0(PyObject* self, PyObject* args)
{
  return PythonScriptContext::RunFunction(self, args, bit_class_name, bitwise_xor_1_0_metadata);
}

static PyObject* python_logical_and_1_0(PyObject* self, PyObject* args)
{
  return PythonScriptContext::RunFunction(self, args, bit_class_name, logical_and_1_0_metadata);
}

static PyObject* python_logical_or_1_0(PyObject* self, PyObject* args)
{
  return PythonScriptContext::RunFunction(self, args, bit_class_name, logical_or_1_0_metadata);
}

static PyObject* python_logical_xor_1_0(PyObject* self, PyObject* args)
{
  return PythonScriptContext::RunFunction(self, args, bit_class_name, logical_xor_1_0_metadata);
}

static PyObject* python_logical_not_1_0(PyObject* self, PyObject* args)
{
  return PythonScriptContext::RunFunction(self, args, bit_class_name, logical_not_1_0_metadata);
}

static PyObject* python_bit_shift_left_1_0(PyObject* self, PyObject* args)
{
  return PythonScriptContext::RunFunction(self, args, bit_class_name, bit_shift_left_1_0_metadata);
}

static PyObject* python_bit_shift_right_1_0(PyObject* self, PyObject* args)
{
  return PythonScriptContext::RunFunction(self, args, bit_class_name, bit_shift_right_1_0_metadata);
}


PyObject* ImportModule(const std::string& api_version)
{
  if (!initialized_bit_module_importer)
  {
    InitFunctionList();
    initialized_bit_module_importer = true;
  }

  std::vector<FunctionMetadata> functions_for_version =
      BitApi::GetClassMetadataForVersion(api_version).functions_list;

  std::vector<PyMethodDef> python_functions_to_register;

  for (auto& functionMetadata : functions_for_version)
  {
    PyCFunction next_function_to_register = nullptr;
    switch ((unsigned long long)functionMetadata.function_pointer)
    {
    case (unsigned long long)BitApi::BitwiseAnd:
      next_function_to_register = python_bitwise_and_1_0;
      break;
    case (unsigned long long)BitApi::BitwiseOr:
      next_function_to_register = python_bitwise_or_1_0;
      break;
    case (unsigned long long)BitApi::BitwiseNot:
      next_function_to_register = python_bitwise_not_1_0;
      break;
    case (unsigned long long)BitApi::BitwiseXor:
      next_function_to_register = python_bitwise_xor_1_0;
      break;
    case (unsigned long long)BitApi::LogicalAnd:
      next_function_to_register = python_logical_and_1_0;
      break;
    case (unsigned long long)BitApi::LogicalOr:
      next_function_to_register = python_logical_or_1_0;
      break;
    case (unsigned long long)BitApi::LogicalXor:
      next_function_to_register = python_logical_xor_1_0;
      break;
    case (unsigned long long)BitApi::LogicalNot:
      next_function_to_register = python_logical_not_1_0;
      break;
    case (unsigned long long)BitApi::BitShiftLeft:
      next_function_to_register = python_bit_shift_left_1_0;
      break;
    case (unsigned long long)BitApi::BitShiftRight:
      next_function_to_register = python_bit_shift_right_1_0;
      break;
    default:
      break;
    }

    if (next_function_to_register == nullptr)
    {
      throw std::invalid_argument(
          fmt::format("Unknown argument inside of BitModuleImporter::ImportModule() for function "
                      "{}. Did you add a new "
                      "function to the BitApi and forget to update the list in this function?",
                      functionMetadata.function_name)
              .c_str());
      return nullptr;
    }

    python_functions_to_register.push_back({functionMetadata.function_name.c_str(),
                                            next_function_to_register, METH_VARARGS,
                                            functionMetadata.example_function_call.c_str()});

  }
  PyModuleDef module_obj = {PyModuleDef_HEAD_INIT, bit_class_name.c_str(), "Bit Module", 0, &python_functions_to_register[0]};
  return PyModule_Create(&module_obj);
}


}
