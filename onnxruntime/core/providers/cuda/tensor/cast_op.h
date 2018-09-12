﻿#pragma once

#include "core/providers/cuda/cuda_common.h"

namespace onnxruntime {
namespace cuda {

template <typename SrcT>
class Cast final : public CudaKernel {
 public:
  Cast(const OpKernelInfo& info) : CudaKernel(info) {
    int64_t to;
    Status status = info.GetAttr("to", &to);
    LOTUS_ENFORCE(status.IsOK(), "Attribute to is not set.");
    to_ = gsl::narrow_cast<ONNX_NAMESPACE::TensorProto_DataType>(to);
  }

  Status ComputeInternal(OpKernelContext* context) const override;

 private:
  ONNX_NAMESPACE::TensorProto_DataType to_;
};

}  // namespace cuda
}  // namespace onnxruntime