// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "test/compare_mlvalue.h"
#include <cmath>
#include <sstream>
#include <google/protobuf/text_format.h>
#include "core/graph/onnx_protobuf.h"

#include "Eigen/Core"
#include "Eigen/src/Core/arch/CUDA/Half.h"

using namespace onnxruntime;

#if (!EIGEN_VERSION_AT_LEAST(3, 3, 6))
  namespace Eigen {
    namespace half_impl {
      using __half_raw = ::Eigen::half_impl::__half;
    }
  }
#endif

#define CASE_TYPE(X)                             \
  case ONNX_NAMESPACE::TensorProto_DataType_##X: \
    return ONNX_TENSOR_ELEMENT_DATA_TYPE_##X;

namespace {

ONNXTensorElementDataType CApiElementTypeFromProto(int type) {
  switch (type) {
    CASE_TYPE(FLOAT)
    CASE_TYPE(UINT8)
    CASE_TYPE(INT8)
    CASE_TYPE(UINT16)
    CASE_TYPE(INT16)
    CASE_TYPE(INT32)
    CASE_TYPE(INT64)
    CASE_TYPE(STRING)
    CASE_TYPE(BOOL)
    CASE_TYPE(FLOAT16)
    CASE_TYPE(DOUBLE)
    CASE_TYPE(UINT32)
    CASE_TYPE(UINT64)
    CASE_TYPE(COMPLEX64)
    CASE_TYPE(COMPLEX128)
    CASE_TYPE(BFLOAT16)
    default:
      return ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
  }
}

template <typename FLOAT_TYPE>
std::pair<COMPARE_RESULT, std::string> CompareFloatResult(const Tensor& outvalue, const Tensor& expected_value,
                                                          double per_sample_tolerance,
                                                          double relative_per_sample_tolerance,
                                                          bool post_processing) {
  const size_t size1 = expected_value.Shape().Size();
  const FLOAT_TYPE* expected_output = expected_value.template Data<FLOAT_TYPE>();
  const FLOAT_TYPE* real_output = outvalue.template Data<FLOAT_TYPE>();
  std::pair<COMPARE_RESULT, std::string> res = std::make_pair(COMPARE_RESULT::SUCCESS, "");
  double max_diff = 0;
  size_t diff_count = 0;
  for (size_t di = 0; di != size1; ++di) {
    const double real_value = post_processing ? std::max<double>(0.0, std::min<double>(255.0, real_output[di]))
                                              : real_output[di];
    const double diff = fabs(expected_output[di] - real_value);
    const double rtol = per_sample_tolerance + relative_per_sample_tolerance * fabs(expected_output[di]);
    if (diff > rtol || (std::isnan(diff) && !std::isnan(expected_output[di]))) {
      res.first = COMPARE_RESULT::RESULT_DIFFERS;
      // update error message if this is a larger diff
      if (diff > max_diff || (std::isnan(diff) && !std::isnan(max_diff))) {
        std::ostringstream oss;
        oss << "expected " << expected_output[di] << ", got " << real_value
            << ", diff: " << diff << ", tol=" << rtol << ".";
        res.second = oss.str();
        max_diff = diff;
      }
      ++diff_count;
    }
  }

  if (res.first == COMPARE_RESULT::SUCCESS) return res;

  std::ostringstream oss;
  oss << res.second << " " << diff_count << " of " << size1 << " differ";
  res.second = oss.str();
  return res;
}

template <typename T>
std::pair<COMPARE_RESULT, std::string> IsResultExactlyMatch(const Tensor& outvalue, const Tensor& expected_value) {
  const size_t size1 = expected_value.Shape().Size();
  const T* expected_output = expected_value.template Data<T>();
  const T* real_output = outvalue.template Data<T>();
  for (size_t di = 0; di != size1; ++di) {
    if (expected_output[di] != real_output[di]) {
      std::ostringstream oss;
      oss << "expected " << expected_output[di] << ", got " << real_output[di];
      return std::make_pair(COMPARE_RESULT::RESULT_DIFFERS, oss.str());
    }
  }
  return std::make_pair(COMPARE_RESULT::SUCCESS, "");
}

std::pair<COMPARE_RESULT, std::string> CompareFloat16Result(const Tensor& outvalue, const Tensor& expected_value,
                                                            double per_sample_tolerance,
                                                            double relative_per_sample_tolerance,
                                                            bool post_processing) {
  const size_t size1 = expected_value.Shape().Size();
  const MLFloat16* expected_output = expected_value.template Data<MLFloat16>();
  const MLFloat16* real_output = outvalue.template Data<MLFloat16>();
  for (size_t di = 0; di != size1; ++di) {
    float expected = Eigen::half_impl::half_to_float(Eigen::half_impl::__half_raw(expected_output[di].val));
    float real = Eigen::half_impl::half_to_float(Eigen::half_impl::__half_raw(real_output[di].val));
    real = post_processing ? std::max(0.0f, std::min(255.0f, real)) : real;
    const double diff = fabs(expected - real);
    const double rtol = per_sample_tolerance + relative_per_sample_tolerance * fabs(expected);
    if (diff > rtol || (std::isnan(diff) && !std::isnan(expected))) {
      std::ostringstream oss;
      oss << "expected " << expected << ", got " << real << ", diff: " << diff << ", tol=" << rtol;

      return std::make_pair(COMPARE_RESULT::RESULT_DIFFERS, oss.str());
    }
  }
  return std::make_pair(COMPARE_RESULT::SUCCESS, "");
}

std::pair<COMPARE_RESULT, std::string> CompareTwoTensors(const Tensor& outvalue, const Tensor& expected_tensor,
                                                         double per_sample_tolerance,
                                                         double relative_per_sample_tolerance,
                                                         bool post_processing) {
  if (expected_tensor.Shape() != outvalue.Shape()) {
    std::ostringstream oss;
    oss << "shape mismatch, expect " << expected_tensor.Shape().ToString() << " got " << outvalue.Shape().ToString();
    return std::make_pair(COMPARE_RESULT::SHAPE_MISMATCH, oss.str());
  }
  auto p1 = outvalue.DataType();
  if (p1 == DataTypeImpl::GetType<float>()) {
    return CompareFloatResult<float>(outvalue, expected_tensor,
                                     per_sample_tolerance, relative_per_sample_tolerance, post_processing);
  } else if (p1 == DataTypeImpl::GetType<double>()) {
    return CompareFloatResult<double>(outvalue, expected_tensor,
                                      per_sample_tolerance, relative_per_sample_tolerance, post_processing);
  } else if (p1 == DataTypeImpl::GetType<std::string>()) {
    return IsResultExactlyMatch<std::string>(outvalue, expected_tensor);
  } else if (p1 == DataTypeImpl::GetType<uint8_t>()) {
    return IsResultExactlyMatch<uint8_t>(outvalue, expected_tensor);
  } else if (p1 == DataTypeImpl::GetType<int8_t>()) {
    return IsResultExactlyMatch<int8_t>(outvalue, expected_tensor);
  } else if (p1 == DataTypeImpl::GetType<uint16_t>()) {
    return IsResultExactlyMatch<uint16_t>(outvalue, expected_tensor);
  } else if (p1 == DataTypeImpl::GetType<int16_t>()) {
    return IsResultExactlyMatch<int16_t>(outvalue, expected_tensor);
  } else if (p1 == DataTypeImpl::GetType<uint32_t>()) {
    return IsResultExactlyMatch<uint32_t>(outvalue, expected_tensor);
  } else if (p1 == DataTypeImpl::GetType<int32_t>()) {
    return IsResultExactlyMatch<int32_t>(outvalue, expected_tensor);
  } else if (p1 == DataTypeImpl::GetType<uint64_t>()) {
    return IsResultExactlyMatch<uint64_t>(outvalue, expected_tensor);
  } else if (p1 == DataTypeImpl::GetType<int64_t>()) {
    return IsResultExactlyMatch<int64_t>(outvalue, expected_tensor);
  } else if (p1 == DataTypeImpl::GetType<bool>()) {
    return IsResultExactlyMatch<bool>(outvalue, expected_tensor);
  } else if (p1 == DataTypeImpl::GetType<MLFloat16>()) {
    return CompareFloat16Result(outvalue, expected_tensor,
                                per_sample_tolerance, relative_per_sample_tolerance, post_processing);
  } else {
    return std::make_pair(COMPARE_RESULT::NOT_SUPPORT, "");
  }
}
template <typename T>
std::pair<COMPARE_RESULT, std::string> CompareSeqOfMapToFloat(const T& real_output_vector, const T& expected_value,
                                                              double per_sample_tolerance,
                                                              double relative_per_sample_tolerance,
                                                              bool post_processing) {
  if (real_output_vector.size() != expected_value.size()) {
    std::ostringstream oss;
    oss << "vector size mismatch, expected " << expected_value.size() << ", got " << real_output_vector.size();
    return std::make_pair(COMPARE_RESULT::RESULT_DIFFERS, oss.str());
  }
  for (size_t i = 0; i != real_output_vector.size(); ++i) {
    const auto& expected_map = expected_value[i];
    //compare if expected_map equals real_output_vector[i]
    if (real_output_vector[i].size() != expected_map.size()) {
      std::ostringstream oss;
      oss << "map size mismatch, expected " << expected_map.size() << ", got " << real_output_vector[i].size();
      return std::make_pair(COMPARE_RESULT::RESULT_DIFFERS, oss.str());
    }

    for (const auto& real_output_key_value_pair : real_output_vector[i]) {
      auto expected_key_value_pair = expected_map.find(real_output_key_value_pair.first);
      if (expected_key_value_pair == expected_map.end()) {
        return std::make_pair(COMPARE_RESULT::RESULT_DIFFERS, "");
      }
      const double real = post_processing
                              ? std::max<double>(0.0, std::min<double>(255.0, real_output_key_value_pair.second))
                              : real_output_key_value_pair.second;
      const double diff = fabs(expected_key_value_pair->second - real);
      const double rtol = per_sample_tolerance + relative_per_sample_tolerance * fabs(expected_key_value_pair->second);
      if (diff > rtol || (std::isnan(diff) && !std::isnan(expected_key_value_pair->second))) {
        std::ostringstream oss;
        oss << "expected " << expected_key_value_pair->second << ", got " << real
            << ", diff: " << diff << ", tol=" << rtol;
        return std::make_pair(COMPARE_RESULT::RESULT_DIFFERS, oss.str());
      }
    }
  }
  return std::make_pair(COMPARE_RESULT::SUCCESS, "");
}

const char* ElementTypeToString(MLDataType type) {
  if (type == DataTypeImpl::GetType<float>()) {
    return "tensor(float)";
  } else if (type == DataTypeImpl::GetType<bool>()) {
    return "tensor(bool)";
  }

  else if (type == DataTypeImpl::GetType<int32_t>()) {
    return "tensor(int32)";
  }

  else if (type == DataTypeImpl::GetType<double>()) {
    return "tensor(double)";
  }

  else if (type == DataTypeImpl::GetType<std::string>()) {
    return "tensor(string)";
  }

  else if (type == DataTypeImpl::GetType<uint8_t>()) {
    return "tensor(uint8)";
  }

  else if (type == DataTypeImpl::GetType<uint16_t>()) {
    return "tensor(uint16)";
  }

  else if (type == DataTypeImpl::GetType<int16_t>()) {
    return "tensor(int16)";
  }

  else if (type == DataTypeImpl::GetType<int64_t>()) {
    return "tensor(int64)";
  }

  else if (type == DataTypeImpl::GetType<uint32_t>()) {
    return "tensor(uint32)";
  }

  else if (type == DataTypeImpl::GetType<uint64_t>()) {
    return "tensor(uint64)";
  }

  else if (type == DataTypeImpl::GetType<MLFloat16>()) {
    return "tensor(MLFloat16)";
  } else {
    return "unknown";
  }
}

//The expected_shape could contain unknown dimensions, but the real_shape cannot
bool AreShapesEqual(const std::vector<int64_t>& real_shape, const ::ONNX_NAMESPACE::TensorShapeProto& expected_shape) {
  const int len = expected_shape.dim_size();
  if (len < 0) return false;
  if (real_shape.size() != static_cast<size_t>(len)) return false;
  for (int i = 0; i != len; ++i) {
    if (!expected_shape.dim(i).has_dim_value()) {
      //symbolic shape, cannot validate it right now, assume it matches every thing
      continue;
    }
    ::google::protobuf::int64 d = expected_shape.dim(i).dim_value();
    if (d != real_shape[i]) return false;
  }
  return true;
}

template <typename T>
std::ostringstream& VectorToString(const std::vector<T>& input, std::ostringstream& oss) {
  size_t len = input.size();
  oss << "[";
  if (len > 0) {
    oss << input[0];
    for (size_t i = 1; i != len; ++i) {
      oss << ", " << input[i];
    }
  }
  oss << "]";
  return oss;
}

}  // namespace

namespace onnxruntime {
std::pair<COMPARE_RESULT, std::string> CompareMLValue(const MLValue& o, const MLValue& expected_mlvalue,
                                                      double per_sample_tolerance,
                                                      double relative_per_sample_tolerance,
                                                      bool post_processing) {
  if (o.IsTensor() != expected_mlvalue.IsTensor() || o.Type() != expected_mlvalue.Type()) {
    return std::make_pair(COMPARE_RESULT::TYPE_MISMATCH, "");
  }
  if (!o.IsTensor()) {
    if (o.Type() == DataTypeImpl::GetType<VectorMapInt64ToFloat>()) {
      return CompareSeqOfMapToFloat(o.Get<VectorMapInt64ToFloat>(), expected_mlvalue.Get<VectorMapInt64ToFloat>(),
                                    per_sample_tolerance, relative_per_sample_tolerance, post_processing);
    }
    if (o.Type() == DataTypeImpl::GetType<VectorMapStringToFloat>()) {
      return CompareSeqOfMapToFloat(o.Get<VectorMapStringToFloat>(), expected_mlvalue.Get<VectorMapStringToFloat>(),
                                    per_sample_tolerance, relative_per_sample_tolerance, post_processing);
    }
    return std::make_pair(COMPARE_RESULT::NOT_SUPPORT, "");
  }
  const Tensor& outvalue = o.Get<Tensor>();
  const Tensor& expected_tensor = expected_mlvalue.Get<Tensor>();
  if (outvalue.DataType() != expected_tensor.DataType()) {
    std::ostringstream oss;
    oss << "expect " << ElementTypeToString(expected_tensor.DataType())
        << " got " << ElementTypeToString(outvalue.DataType());
    return std::make_pair(COMPARE_RESULT::TYPE_MISMATCH, oss.str());
  }
  return CompareTwoTensors(outvalue, expected_tensor,
                           per_sample_tolerance, relative_per_sample_tolerance, post_processing);
}

std::pair<COMPARE_RESULT, std::string> VerifyValueInfo(const ONNX_NAMESPACE::ValueInfoProto& v, const OrtValue* o) {
  if (!v.has_type()) return std::make_pair(COMPARE_RESULT::SUCCESS, "");
  if (v.type().has_tensor_type()) {
    if (OrtIsTensor(o) == 0) {
      return std::make_pair(COMPARE_RESULT::TYPE_MISMATCH, "");
    }

    ::ONNX_NAMESPACE::TypeProto_Tensor t = v.type().tensor_type();
    //below code doesn't work
    //if (((TensorTypeBase*)o.Type())->GetElementType() != DataTypeImpl::ElementTypeFromProto(t.elem_type())) {
    //	return COMPARE_RESULT::TYPE_MISMATCH;
    //}
    std::unique_ptr<OrtTensorTypeAndShapeInfo> info;
    {
      OrtTensorTypeAndShapeInfo* t1;
      ORT_THROW_ON_ERROR(OrtGetTensorShapeAndType(o, &t1));
      info.reset(t1);
    }
    ONNXTensorElementDataType real_type = OrtGetTensorElementType(info.get());
    ONNXTensorElementDataType expected_type = CApiElementTypeFromProto(t.elem_type());
    if (real_type != expected_type) {
      return std::make_pair(COMPARE_RESULT::TYPE_MISMATCH, "");
    }
    std::vector<int64_t> shape = GetTensorShape(info.get());
    if (!AreShapesEqual(shape, t.shape())) {
      std::string result;
      if (!google::protobuf::TextFormat::PrintToString(t.shape(), &result)) {
        result = "(unknown)";
      }
      std::ostringstream oss;
      oss << "Tensor shape mismatch, model file expects '" << result << "', real output is ";
      VectorToString(shape, oss);
      return std::make_pair(COMPARE_RESULT::SHAPE_MISMATCH, oss.str());
    }
  } else {
    //Cannot do this check for tensor type.
    //For tensor type, o.Type() is TensorTypeBase*, but p points to a subclass of TensorTypeBase
    auto p = DataTypeImpl::TypeFromProto(v.type());
    if (((MLValue*)o)->Type() != p) {
      return std::make_pair(COMPARE_RESULT::TYPE_MISMATCH, "");
    }
  }
  return std::make_pair(COMPARE_RESULT::SUCCESS, "");
}
}  // namespace onnxruntime
