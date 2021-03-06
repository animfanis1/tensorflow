/* Copyright 2018 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/contrib/lite/delegates/eager/test_util.h"

#include "absl/memory/memory.h"
#include "flatbuffers/flexbuffers.h"  // flatbuffers
#include "tensorflow/contrib/lite/string.h"

namespace tflite {
namespace eager {
namespace testing {

bool EagerModelTest::Invoke() { return interpreter_->Invoke() == kTfLiteOk; }

void EagerModelTest::SetValues(int tensor_index,
                               const std::vector<float>& values) {
  float* v = interpreter_->typed_tensor<float>(tensor_index);
  for (float f : values) {
    *v++ = f;
  }
}

std::vector<float> EagerModelTest::GetValues(int tensor_index) {
  TfLiteTensor* o = interpreter_->tensor(tensor_index);
  return std::vector<float>(o->data.f, o->data.f + o->bytes / sizeof(float));
}

void EagerModelTest::SetShape(int tensor_index,
                              const std::vector<int>& values) {
  ASSERT_EQ(interpreter_->ResizeInputTensor(tensor_index, values), kTfLiteOk);
  ASSERT_EQ(interpreter_->AllocateTensors(), kTfLiteOk);
}

std::vector<int> EagerModelTest::GetShape(int tensor_index) {
  std::vector<int> result;
  auto* dims = interpreter_->tensor(tensor_index)->dims;
  result.reserve(dims->size);
  for (int i = 0; i < dims->size; ++i) {
    result.push_back(dims->data[i]);
  }
  return result;
}

void EagerModelTest::AddTensors(int num_tensors, const std::vector<int>& inputs,
                                const std::vector<int>& outputs,
                                const TfLiteType& type,
                                const std::vector<int>& dims) {
  interpreter_->AddTensors(num_tensors);
  for (int i = 0; i < num_tensors; ++i) {
    TfLiteQuantizationParams quant;
    CHECK_EQ(interpreter_->SetTensorParametersReadWrite(i, type,
                                                        /*name=*/"",
                                                        /*dims=*/dims, quant),
             kTfLiteOk);
  }

  CHECK_EQ(interpreter_->SetInputs(inputs), kTfLiteOk);
  CHECK_EQ(interpreter_->SetOutputs(outputs), kTfLiteOk);
}

void EagerModelTest::AddTfLiteMulOp(const std::vector<int>& inputs,
                                    const std::vector<int>& outputs) {
  static TfLiteRegistration reg = {nullptr, nullptr, nullptr, nullptr};
  reg.builtin_code = BuiltinOperator_MUL;
  reg.prepare = [](TfLiteContext* context, TfLiteNode* node) {
    auto* i0 = &context->tensors[node->inputs->data[0]];
    auto* o = &context->tensors[node->outputs->data[0]];
    return context->ResizeTensor(context, o, TfLiteIntArrayCopy(i0->dims));
  };
  reg.invoke = [](TfLiteContext* context, TfLiteNode* node) {
    auto* i0 = &context->tensors[node->inputs->data[0]];
    auto* i1 = &context->tensors[node->inputs->data[1]];
    auto* o = &context->tensors[node->outputs->data[0]];
    for (int i = 0; i < o->bytes / sizeof(float); ++i) {
      o->data.f[i] = i0->data.f[i] * i1->data.f[i];
    }
    return kTfLiteOk;
  };

  CHECK_EQ(interpreter_->AddNodeWithParameters(inputs, outputs, nullptr, 0,
                                               nullptr, &reg),
           kTfLiteOk);
}

void EagerModelTest::AddTfOp(TfOpType op, const std::vector<int>& inputs,
                             const std::vector<int>& outputs) {
  auto attr = [](const string& key, const string& value) {
    return " attr{ key: '" + key + "' value {" + value + "}}";
  };

  if (op == kUnpack) {
    string attributes = attr("T", "type: DT_FLOAT") + attr("num", "i: 2") +
                        attr("axis", "i: 0");
    AddTfOp("EagerUnpack", "Unpack", attributes, inputs, outputs);
  } else if (op == kIdentity) {
    string attributes = attr("T", "type: DT_FLOAT");
    AddTfOp("EagerIdentity", "Identity", attributes, inputs, outputs);
  } else if (op == kAdd) {
    string attributes = attr("T", "type: DT_FLOAT");
    AddTfOp("EagerAdd", "Add", attributes, inputs, outputs);
  } else if (op == kMul) {
    string attributes = attr("T", "type: DT_FLOAT");
    AddTfOp("EagerMul", "Mul", attributes, inputs, outputs);
  } else if (op == kNonExistent) {
    AddTfOp("NonExistentOp", "NonExistentOp", "", inputs, outputs);
  } else if (op == kIncompatibleNodeDef) {
    // "Cast" op is created without attributes - making it incompatible.
    AddTfOp("EagerCast", "Cast", "", inputs, outputs);
  }
}

void EagerModelTest::AddTfOp(const char* tflite_name, const string& tf_name,
                             const string& nodedef_str,
                             const std::vector<int>& inputs,
                             const std::vector<int>& outputs) {
  static TfLiteRegistration reg = {nullptr, nullptr, nullptr, nullptr};
  reg.builtin_code = BuiltinOperator_CUSTOM;
  reg.custom_name = tflite_name;

  tensorflow::NodeDef nodedef;
  CHECK(tensorflow::protobuf::TextFormat::ParseFromString(
      nodedef_str + " op: '" + tf_name + "'", &nodedef));
  string serialized_nodedef;
  CHECK(nodedef.SerializeToString(&serialized_nodedef));
  flexbuffers::Builder fbb;
  fbb.Vector([&]() {
    fbb.String(nodedef.op());
    fbb.String(serialized_nodedef);
  });
  fbb.Finish();

  flexbuffers_.push_back(fbb.GetBuffer());
  auto& buffer = flexbuffers_.back();
  CHECK_EQ(interpreter_->AddNodeWithParameters(
               inputs, outputs, reinterpret_cast<const char*>(buffer.data()),
               buffer.size(), nullptr, &reg),
           kTfLiteOk);
}

}  // namespace testing
}  // namespace eager
}  // namespace tflite
