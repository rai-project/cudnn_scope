#define BENCHMARK_NAME "CUDNN/CONV_BWD_BIAS"

#include <benchmark/benchmark.h>

#include <cmath>
#include <iostream>
#include <mutex>
#include <numeric>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

#include <cudnn.h>

#include "args.hpp"
#include "error.hpp"
#include "helper.hpp"
#include "init.hpp"
#include "utils.hpp"

// https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnConvolutionBackwardBias
template <typename T>
static void LAYER_CUDNN_CONV_BWD_BIAS_Impl(benchmark::State& state) {
  if (!has_cuda) {
    state.SkipWithError(BENCHMARK_NAME " no CUDA device found");
    return;
  }
  MEM_ALIGNED_128 const T alpha = detail::one<T>();
  MEM_ALIGNED_128 const T beta  = detail::zero<T>();

  //  w, h, c, n, k, filter_w(s), filter_h(r), pad_w, pad_h, wstride, hstride
  const auto batch_size      = state.range(0);
  const auto channels        = state.range(1);
  const auto height          = state.range(2);
  const auto width           = state.range(3);
  const auto num_filters     = state.range(4);
  const auto filter_width    = state.range(5);
  const auto filter_height   = state.range(6);
  const auto pad_width       = state.range(7);
  const auto pad_height      = state.range(8);
  const auto stride_width    = state.range(9);
  const auto stride_height   = state.range(10);
  const auto dilation_height = state.range(11);
  const auto dilation_width  = state.range(12);

  const auto out_n = batch_size;
  const auto out_w = detail::calc_conv_out_dim(width, filter_width, pad_width, stride_width, dilation_width);
  const auto out_h = detail::calc_conv_out_dim(height, filter_height, pad_height, stride_height, dilation_height);
  const auto out_c = num_filters;

  MEM_ALIGNED_128 auto db_tensor = Tensor<T>(state, {out_c});
  if (!db_tensor.is_valid) {
    return;
  }
  MEM_ALIGNED_128 cudnnTensorDescriptor_t db_descriptor = db_tensor.get();

  MEM_ALIGNED_128 auto dy_tensor = Tensor<T>(state,
                                             {/*batch_size=*/out_n,
                                              /*channels=*/out_c,
                                              /*image_height=*/out_h,
                                              /*image_width=*/out_w});
  if (!dy_tensor.is_valid) {
    return;
  }
  MEM_ALIGNED_128 cudnnTensorDescriptor_t dy_descriptor = dy_tensor.get();

  const auto output_bytes = sizeof(T) * out_n * out_c * out_h * out_w;
  auto output             = std::vector<T>(output_bytes / sizeof(T));
  std::fill(output.begin(), output.end(), detail::one<T>());

  const int bias_bytes = 1 * out_c * 1 * 1 * sizeof(T);
  auto bias            = std::vector<T>(bias_bytes / sizeof(T));
  std::fill(bias.begin(), bias.end(), detail::one<T>());

  MEM_ALIGNED_128 DeviceMemory<T> dy_memory(state, output.data(), output_bytes);
  if (!dy_memory.is_valid) {
    return;
  }
  MEM_ALIGNED_128 const auto d_dy = dy_memory.get();

  MEM_ALIGNED_128 DeviceMemory<T> db_memory(state, bias_bytes);
  if (!db_memory.is_valid) {
    return;
  }
  MEM_ALIGNED_128 const auto d_db = db_memory.get();

  cudnnStatus_t cudnn_err;
  BENCHMARK_BLOCK(cudnn_err, {
    cudnn_err = cudnnConvolutionBackwardBias(cudnn_handle, &alpha, dy_descriptor, d_dy, &beta, db_descriptor, d_db);
  });

  state.counters.insert({{"input_size", batch_size * channels * height * width},
                         {"input_height", height},
                         {"input_width", width},
                         {"input_channels", channels},
                         {"input_batch_size", batch_size},
                         {"num_filters", num_filters},
                         {"filter_height", filter_height},
                         {"filter_width", filter_width},
                         {"pad_height", pad_height},
                         {"pad_width", pad_width},
                         {"stride_height", stride_height},
                         {"stride_width", stride_width},
                         {"dilation_height", dilation_height},
                         {"dilation_width", dilation_width},
                         {"output_size", out_n * out_c * out_h * out_w},
                         {"output_height", out_h},
                         {"output_width", out_w},
                         {"output_channels", out_c},
                         {"x_tensor_layout", (int) x_tensor.layout},
                         {"y_tensor_layout", (int) y_tensor.layout},
                         {"w_filter_layout", (int) w_filter.layout},
                         {"output_batch_size", out_n}});

  const auto N = batch_size, K = num_filters, C = channels, H = height, W = width, R = filter_height, S = filter_width;

  state.SetItemsProcessed(int64_t(state.iterations()) * N * K * C * W * H);
}

#ifdef GENERATED_BENCHMARK_LAYER

#define ENABLE_LAYER_CUDNN_CONV_BWD_BIAS 1

#include "generated_benchmarks.hpp"

#undef ENABLE_LAYER_CUDNN_CONV_BWD_BIAS

#else // GENERATED_BENCHMARK_LAYER

static void LAYER_CUDNN_CONV_BWD_BIAS_INT8(benchmark::State& state) {
  LAYER_CUDNN_CONV_BWD_BIAS_Impl<int8_t>(state);
}

static void LAYER_CUDNN_CONV_BWD_BIAS_INT32(benchmark::State& state) {
  LAYER_CUDNN_CONV_BWD_BIAS_Impl<int32_t>(state);
}

static void LAYER_CUDNN_CONV_BWD_BIAS_HALF(benchmark::State& state) {
  LAYER_CUDNN_CONV_BWD_BIAS_Impl<__half>(state);
}

static void LAYER_CUDNN_CONV_BWD_BIAS_FLOAT(benchmark::State& state) {
  LAYER_CUDNN_CONV_BWD_BIAS_Impl<float>(state);
}

static void LAYER_CUDNN_CONV_BWD_BIAS_DOUBLE(benchmark::State& state) {
  LAYER_CUDNN_CONV_BWD_BIAS_Impl<double>(state);
}

#define CONV_PROBLEMS ALL_INFERENCE_SERVER_CONV_PROBLEMS

// BENCHMARK_CUDNN(LAYER_CUDNN_CONV_BWD_BIAS_INT8)->INFERENCE_SERVER_CONV_PROBLEMS()->UseManualTime();
// BENCHMARK_CUDNN(LAYER_CUDNN_CONV_BWD_BIAS_INT32)->INFERENCE_SERVER_CONV_PROBLEMS()->UseManualTime();
BENCHMARK_CUDNN(LAYER_CUDNN_CONV_BWD_BIAS_HALF)->INFERENCE_SERVER_CONV_PROBLEMS()->UseManualTime();
BENCHMARK_CUDNN(LAYER_CUDNN_CONV_BWD_BIAS_FLOAT)->INFERENCE_SERVER_CONV_PROBLEMS()->UseManualTime();
// BENCHMARK_CUDNN(LAYER_CUDNN_CONV_BWD_BIAS_DOUBLE)->INFERENCE_SERVER_CONV_PROBLEMS()->UseManualTime();

#endif // GENERATED_BENCHMARK_LAYER
