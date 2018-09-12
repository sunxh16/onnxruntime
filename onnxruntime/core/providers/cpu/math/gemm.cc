#include "core/providers/cpu/math/gemm.h"

namespace onnxruntime {

ONNX_CPU_OPERATOR_KERNEL(
    Gemm,
    7,
    KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<float>()),
    Gemm<float, float, float, float>);
}