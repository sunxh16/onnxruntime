#include "core/providers/cpu/reduction/reduction_ops.h"

namespace Lotus {

REGISTER_KERNEL(KernelDefBuilder("ReduceL1")
                    .Domain(LotusIR::kOnnxDomain)
                    .SinceVersion(1)
                    .Provider(LotusIR::kCpuExecutionProvider)
                    .TypeConstraint("T", DataTypeImpl::GetTensorType<float>()),
                ReduceL1<float>);

REGISTER_KERNEL(KernelDefBuilder("ReduceL2")
                    .Domain(LotusIR::kOnnxDomain)
                    .SinceVersion(1)
                    .Provider(LotusIR::kCpuExecutionProvider)
                    .TypeConstraint("T", DataTypeImpl::GetTensorType<float>()),
                ReduceL2<float>);

REGISTER_KERNEL(KernelDefBuilder("ReduceProd")
                    .Domain(LotusIR::kOnnxDomain)
                    .SinceVersion(1)
                    .Provider(LotusIR::kCpuExecutionProvider)
                    .TypeConstraint("T", DataTypeImpl::GetTensorType<float>()),
                ReduceProd<float>);

void ReduceKernel::PrepareForReduce(OpKernelContext* ctx,
                                    std::vector<float>& transposedInputData,
                                    Tensor** reducedTensor,
                                    int64_t& block_size,
                                    int64_t& blocks) const {
  const Tensor& input = *ctx->Input<Tensor>(0);

  size_t ndim = input.Shape().GetDims().size();
  for (int i = 0; i < axes_.size(); ++i) {
    LOTUS_ENFORCE(axes_[i] >= 0 && axes_[i] < (int64_t)ndim, "Axis attribute out of range");
  }

  transposedInputData.resize(input.Shape().Size(), 0);

  std::vector<int64_t> axes = axes_;
  std::sort(axes.begin(), axes.end());

  vector<bool> keep_axis(ndim, true);
  for (auto i : axes) {
    keep_axis[i] = false;
  }

  //transpose the input so that all to-be-reduced axes are at the head
  vector<int64_t> transposed_axes(axes.begin(), axes.end());
  for (int i = 0; i < ndim; ++i) {
    if (keep_axis[i]) {
      transposed_axes.push_back(i);
    }
  }

  vector<int64_t> new_dims_(transposed_axes.size());
  for (int i = 0; i < transposed_axes.size(); ++i) {
    new_dims_[i] = input.Shape().GetDims().at(transposed_axes[i]);
  }

  int num_axes = static_cast<int>(transposed_axes.size());
  auto in_dims = input.Shape().GetDims();

  // Measure amount of contiguous data we can copy at once
  int64_t blocksize = 1;
  int n_shared_idxs = 0;
  for (int i = num_axes - 1; i >= 0; --i) {
    if (transposed_axes[i] == i) {
      blocksize *= new_dims_[i];
      ++n_shared_idxs;
    } else {
      break;
    }
  }

  const float* from_data = input.Data<float>();
  float* to_data = &transposedInputData[0];
  size_t count = input.Shape().Size();

  if (num_axes < 2 || n_shared_idxs == num_axes) {
    memcpy(to_data, from_data, count * sizeof(float));
    block_size = 1;
    blocks = (int)count;
    return;
  }

  int itr_axes = num_axes - n_shared_idxs;

  // Calculate strides
  std::vector<int64_t> stride_x(itr_axes, 0);
  for (size_t i = 0; i < itr_axes; i++) {
    stride_x[i] = 1;
    for (size_t j = transposed_axes[i] + 1; j < itr_axes; j++) {
      stride_x[i] *= in_dims[j];
    }
  }

  std::vector<int64_t> itr_idxs(itr_axes, 0);

  // Branch here to avoid branching within the loop
  if (blocksize > 1) {
    for (size_t index = 0; index < (count / blocksize); index++) {
      int64_t from_index = 0;
      for (int i = 0; i < itr_axes; ++i) {
        from_index += stride_x[i] * itr_idxs[i];
      }

      memcpy(
          to_data + blocksize * index,
          from_data + blocksize * from_index,
          blocksize * sizeof(float));

      ++itr_idxs[itr_axes - 1];
      for (int i = itr_axes - 1; i >= 1; --i) {
        auto expected_dim = new_dims_[i];
        if (itr_idxs[i] < expected_dim) {
          break;
        }
        itr_idxs[i] %= expected_dim;
        ++itr_idxs[i - 1];
      }
    }
  } else {
    for (size_t index = 0; index < count; index++) {
      int64_t from_index = 0;
      for (int i = 0; i < itr_axes; ++i) {
        from_index += stride_x[i] * itr_idxs[i];
      }

      *(to_data + index) = *(from_data + from_index);

      ++itr_idxs[itr_axes - 1];
      for (int i = itr_axes - 1; i >= 1; --i) {
        auto expected_dim = new_dims_[i];
        if (itr_idxs[i] < expected_dim) {
          break;
        }
        itr_idxs[i] %= expected_dim;
        ++itr_idxs[i - 1];
      }
    }
  }

  //set to-be-reduced axes to one. squeeze is keepdims_ is false
  int64_t first_dim = 1;
  std::vector<int64_t> reduced_dims;
  for (int i = 0; i < in_dims.size(); i++) {
    if (keep_axis[i]) {
      reduced_dims.push_back(in_dims[i]);
    } else {
      first_dim *= in_dims[i];
      if (keepdims_) {
        reduced_dims.push_back(1);
      }
    }
  }

  *reducedTensor = ctx->Output(0, reduced_dims);
  block_size = input.Shape().Size() / first_dim;
  blocks = first_dim;
}

template <>
Status ReduceL1<float>::Compute(OpKernelContext* ctx) const {
  std::vector<float> transposedInputData;
  int64_t block_size, blocks;
  Tensor* reduced;
  PrepareForReduce(ctx, transposedInputData, &reduced, block_size, blocks);

  float* output_data = reduced->MutableData<float>();

  for (int j = 0; j < block_size; ++j) {
    float abs_sum = 0;
    for (int i = 0; i < blocks; ++i) {
      abs_sum += std::abs(transposedInputData[i * block_size + j]);
    }
    *(output_data++) = abs_sum;
  }
  return Status::OK();
}

template <>
Status ReduceL2<float>::Compute(OpKernelContext* ctx) const {
  std::vector<float> transposedInputData;
  int64_t block_size, blocks;
  Tensor* reduced;
  PrepareForReduce(ctx, transposedInputData, &reduced, block_size, blocks);

  float* output_data = reduced->MutableData<float>();

  for (int j = 0; j < block_size; ++j) {
    float square_sum = 0;
    for (int i = 0; i < blocks; ++i) {
      square_sum += std::pow(transposedInputData[i * block_size + j], 2);
    }
    *(output_data++) = std::sqrt(square_sum);
  }
  return Status::OK();
}

template <>
Status ReduceProd<float>::Compute(OpKernelContext* ctx) const {
  std::vector<float> transposedInputData;
  int64_t block_size, blocks;
  Tensor* reduced;
  PrepareForReduce(ctx, transposedInputData, &reduced, block_size, blocks);

  float* output_data = reduced->MutableData<float>();

  for (int j = 0; j < block_size; ++j) {
    float prod = 1;
    for (int i = 0; i < blocks; ++i) {
      prod *= transposedInputData[i * block_size + j];
    }
    *(output_data++) = prod;
  }
  return Status::OK();
}
}  // namespace Lotus