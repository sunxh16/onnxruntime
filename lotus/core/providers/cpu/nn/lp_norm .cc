#include "core/providers/cpu/nn/lp_norm.h"
#include "core/util/math_cpuonly.h"

namespace Lotus {
REGISTER_KERNEL(KernelDefBuilder("LpNormalization")
                    .Domain(LotusIR::kOnnxDomain)
                    .SinceVersion(1)
                    .Provider(LotusIR::kCpuExecutionProvider)
                    .TypeConstraint("T", DataTypeImpl::GetTensorType<float>()),
                LpNorm<float>);

using InnerStride = Eigen::InnerStride<Eigen::Dynamic>;
using StridedVec = Eigen::Map<Eigen::Matrix<float, 1, Eigen::Dynamic>, 0, InnerStride>;
using ConstStridedVec = Eigen::Map<const Eigen::Matrix<float, 1, Eigen::Dynamic>, 0, InnerStride>;

void DoNormalizeP2(
    const float* xData,
    float* yData,
    const int64_t m,
    const int64_t n,
    const int64_t sf) {
  for (int i = 0; i < n; ++i) {
    auto base = (i / sf) * sf * m + (i % sf);
    ConstStridedVec xVec(xData + base, 1, m, InnerStride(sf));
    auto norm = xVec.template lpNorm<2>();
    if (norm != 0) {
      StridedVec yVec(yData + base, 1, m, InnerStride(sf));
      yVec = xVec / norm;
    }
  }
};

void DoNormalizeP1(
    const float* xData,
    float* yData,
    const int64_t m,
    const int64_t n,
    const int64_t sf) {
  for (int i = 0; i < n; ++i) {
    auto base = (i / sf) * sf * m + (i % sf);
    ConstStridedVec xVec(xData + base, 1, m, InnerStride(sf));
    auto norm = xVec.template lpNorm<1>();
    if (norm != 0) {
      StridedVec yVec(yData + base, 1, m, InnerStride(sf));
      yVec = xVec / norm;
    }
  }
};

template <>
Status LpNorm<float>::Compute(OpKernelContext* p_op_kernel_context) const {
  const Tensor* input = p_op_kernel_context->Input<Tensor>(0);
  const TensorShape& input_shape = input->Shape();
  Tensor* output = p_op_kernel_context->Output(0, input_shape);

  const auto canonical_axis = axis_ != -1 ? axis_ : (input_shape.NumDimensions() - 1);
  const int64_t m = input_shape.GetDims()[canonical_axis];
  const int64_t n = input_shape.Size() / m;
  const int64_t sf = input_shape.SizeFromDimension(canonical_axis + 1);

  if (p_ == 1) {
    DoNormalizeP1(input->Data<float>(), output->MutableData<float>(), m, n, sf);
  } else if (p_ == 2) {
    DoNormalizeP2(input->Data<float>(), output->MutableData<float>(), m, n, sf);
  }

  return Status::OK();
}
}  // namespace Lotus
