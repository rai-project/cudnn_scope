#define BENCHMARK_NAME "CUDNN/CONV_BWD_DATA"

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

// http://www.goldsborough.me/cuda/ml/cudnn/c++/2017/10/01/14-37-23-convolutions_with_cudnn/
// https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnConvolutionBwdDataAlgo_t
// https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnConvolutionBackwardData
template <typename T, cudnnConvolutionBwdDataAlgo_t convolution_algorithm
#ifdef CUDNN_SUPPORTS_TENSOR_OPS
          ,
          cudnnMathType_t math_type = CUDNN_DEFAULT_MATH
#endif // CUDNN_SUPPORTS_TENSOR_OPS
          >
static void iLAYER_CUDNN_CONV_BWD_DATA_Impl(benchmark::State& state) {
  if (!has_cuda) {
    state.SkipWithError(BENCHMARK_NAME " no CUDA device found");
    return;
  }
#ifdef CUDNN_SUPPORTS_TENSOR_OPS
  if (math_type == CUDNN_TENSOR_OP_MATH && !detail::SupportsTensorCore(cuda_device_id)) {
    state.SkipWithError(BENCHMARK_NAME "no Tensorcore support on current device");
    return;
  }
#endif // CUDNN_SUPPORTS_TENSOR_OPS

  MEM_ALIGNED_128 const T alpha          = detail::one<T>();
  MEM_ALIGNED_128 const T beta           = detail::zero<T>();
  const cudnnConvolutionMode_t conv_mode = CUDNN_CONVOLUTION;

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
  const auto group           = state.range(13) == 0 ? 1 : state.range(13);

  MEM_ALIGNED_128 cudnnConvolutionDescriptor_t convolution_descriptor;
  if (PRINT_IF_ERROR(cudnnCreateConvolutionDescriptor(&convolution_descriptor))) {
    state.SkipWithError(BENCHMARK_NAME " failed to cudnnCreateConvolutionDescriptor");
    return;
  }
  if (PRINT_IF_ERROR(cudnnSetConvolution2dDescriptor(convolution_descriptor,
                                                     /*pad_height=*/pad_height,
                                                     /*pad_width=*/pad_width,
                                                     /*vertical_stride=*/stride_height,
                                                     /*horizontal_stride=*/stride_width,
                                                     /*dilation_height=*/dilation_height,
                                                     /*dilation_width=*/dilation_width,
                                                     /*mode=*/conv_mode,
                                                     /*computeType=*/accumDataType<T>::type))) {
    state.SkipWithError(BENCHMARK_NAME " failed to cudnnSetConvolution2dDescriptor");
    return;
  }
  defer(cudnnDestroyConvolutionDescriptor(convolution_descriptor));

#ifdef CUDNN_SUPPORTS_TENSOR_OPS
  if (PRINT_IF_ERROR(cudnnSetConvolutionMathType(convolution_descriptor, math_type))) {

    state.SkipWithError(BENCHMARK_NAME " failed to cudnnSetConvolutionMathType");
    return;
  }
#endif // CUDNN_SUPPORTS_TENSOR_OPS

  if (PRINT_IF_ERROR(cudnnSetConvolutionGroupCount(convolution_descriptor, group))) {

    state.SkipWithError(BENCHMARK_NAME " failed to cudnnSetConvolutionGroupCount");
    return;
  }

  MEM_ALIGNED_128 auto dx_tensor = Tensor<T>(state,
                                             {/*batch_size=*/batch_size,
                                              /*channels=*/channels,
                                              /*image_height=*/height,
                                              /*image_width=*/width});
  if (!dx_tensor.is_valid) {
    return;
  }
  MEM_ALIGNED_128 cudnnTensorDescriptor_t dx_descriptor = dx_tensor.get();

  MEM_ALIGNED_128 const auto w_filter = Filter<T>(state,
                                                  {/*out_channels=*/num_filters,
                                                   /*in_channels=*/channels,
                                                   /*kernel_height=*/filter_height,
                                                   /*kernel_width=*/filter_width});
  if (!w_filter.is_valid) {
    return;
  }
  MEM_ALIGNED_128 cudnnFilterDescriptor_t w_descriptor = w_filter.get();

  int out_n, out_c, out_h, out_w;
  const auto cudnn_get_conv_output_err = cudnnGetConvolution2dForwardOutputDim(
      convolution_descriptor, dx_descriptor, w_descriptor, &out_n, &out_c, &out_h, &out_w);
  if (PRINT_IF_ERROR(cudnn_get_conv_output_err)) {
    state.SkipWithError(fmt::format(BENCHMARK_NAME " failed to cudnnGetConvolution2dForwardOutputDim because of {}",
                                    utils::detail::error_string(cudnn_get_conv_output_err))
                            .c_str());
    return;
  }

  MEM_ALIGNED_128 auto dy_tensor = Tensor<T>(state,
                                             {/*batch_size=*/out_n,
                                              /*channels=*/out_c,
                                              /*image_height=*/out_h,
                                              /*image_width=*/out_w});
  if (!dy_tensor.is_valid) {
    return;
  }
  MEM_ALIGNED_128 cudnnTensorDescriptor_t dy_descriptor = dy_tensor.get();

  MEM_ALIGNED_128 cudnnConvolutionBwdDataAlgo_t advised_convolution_algorithm = (cudnnConvolutionBwdDataAlgo_t) -1;
  if (IS_ERROR(cudnnGetConvolutionBackwardDataAlgorithm(
          cudnn_handle, w_descriptor, dy_descriptor, convolution_descriptor, dx_descriptor,
          CUDNN_CONVOLUTION_BWD_DATA_PREFER_FASTEST, 0, &advised_convolution_algorithm))) {
    advised_convolution_algorithm = (cudnnConvolutionBwdDataAlgo_t) -1;
  }

  MEM_ALIGNED_128 size_t workspace_bytes = 0;
  if (std::is_same<T, int8_t>::value) {

    // Note: cudnn workspace size function doesn't work for INT8_CONFIG
    workspace_bytes = 1073741824;
  } else {
    if (PRINT_IF_ERROR(cudnnGetConvolutionBackwardDataWorkspaceSize(cudnn_handle,
                                                                    w_descriptor,
                                                                    dy_descriptor,
                                                                    convolution_descriptor,
                                                                    dx_descriptor,
                                                                    convolution_algorithm,
                                                                    &workspace_bytes))) {
      workspace_bytes = 1073741824;
      // state.SkipWithError(BENCHMARK_NAME " failed to cudnnGetConvolutionBackwardDataWorkspaceSize");
      // return;
    }
  }
  // std::cerr << "Workspace size: " << (workspace_bytes / 1048576.0) << "MB" << std::endl;

  const int input_bytes  = batch_size * channels * height * width * sizeof(T);
  const int kernel_bytes = num_filters * channels * filter_height * filter_width * sizeof(T);
  auto input             = std::vector<T>(input_bytes / sizeof(T));
  auto kernel            = std::vector<T>(kernel_bytes / sizeof(T));
  std::fill(input.begin(), input.end(), detail::one<T>());
  std::fill(kernel.begin(), kernel.end(), detail::one<T>());

  const auto output_bytes = sizeof(T) * out_n * out_c * out_h * out_w;
  auto output             = std::vector<T>(output_bytes / sizeof(T));
  std::fill(output.begin(), output.end(), detail::one<T>());

  MEM_ALIGNED_128 DeviceMemory<T> workspace_memory(state, workspace_bytes);
  if (!workspace_memory.is_valid) {
    return;
  }
  MEM_ALIGNED_128 const auto d_workspace = workspace_memory.get();

  MEM_ALIGNED_128 DeviceMemory<T> w_memory(state, kernel.data(), kernel_bytes);
  if (!w_memory.is_valid) {
    return;
  }
  MEM_ALIGNED_128 const auto d_w = w_memory.get();

  MEM_ALIGNED_128 DeviceMemory<T> dy_memory(state, output.data(), output_bytes);
  if (!dy_memory.is_valid) {
    return;
  }
  MEM_ALIGNED_128 const auto d_dy = dy_memory.get();

  MEM_ALIGNED_128 DeviceMemory<T> dx_memory(state, input_bytes);
  if (!dx_memory.is_valid) {
    return;
  }
  MEM_ALIGNED_128 const auto d_dx = dx_memory.get();

  cudnnStatus_t cudnn_err;
  BENCHMARK_BLOCK(cudnn_err, {
    cudnn_err = cudnnConvolutionBackwardData(cudnn_handle, &alpha, w_descriptor, d_w, dy_descriptor, d_dy,
                                             convolution_descriptor, convolution_algorithm, d_workspace,
                                             workspace_bytes, &beta, dx_descriptor, d_dx);
  });

  state.counters.insert({{"input_size", batch_size * channels * height * width},
                         {"input_batch_size", batch_size},
                         {"input_channels", channels},
                         {"input_height", height},
                         {"input_width", width},
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
                         {"output_batch_size", out_n},
                         {"output_channels", out_c},
                         {"output_height", out_h},
                         {"output_width", out_w},
                         {"workspace_bytes", workspace_bytes},
                         {"workspace_megabytes", workspace_bytes / 1048576.0},
                         {"convolution_algorithm", (int) convolution_algorithm},
                         {"advised_convolution_algorithm", (int) advised_convolution_algorithm},
                         {"x_tensor_layout", (int) x_tensor.layout},
                         {"y_tensor_layout", (int) y_tensor.layout},
                         {"w_filter_layout", (int) w_filter.layout},
                         {"math_type", (int) 0}});

  const auto N = batch_size, K = num_filters, C = channels, H = height, W = width, R = filter_height, S = filter_width;
  const auto P = out_h, Q = out_w;

  const auto compute_flops = [&](cudnnConvolutionBwdDataAlgo_t alg) {
    switch (alg) {
      case CUDNN_CONVOLUTION_BWD_DATA_ALGO_0:
      case CUDNN_CONVOLUTION_BWD_DATA_ALGO_1:
        // flops = filter_width * filter_height * out_w * out_h * channels * out_c * batch_size *
        // state.iterations(); 2KCRSNPQ
        return static_cast<double>(K) * static_cast<double>(C) * static_cast<double>(R) * static_cast<double>(S) *
               static_cast<double>(N) * static_cast<double>(P) * static_cast<double>(Q);
      case CUDNN_CONVOLUTION_BWD_DATA_ALGO_FFT:
      case CUDNN_CONVOLUTION_BWD_DATA_ALGO_FFT_TILING:
        //(NCKHW + (NC +CK +NK)HW log(HW))
        return (static_cast<double>(N) * static_cast<double>(C) * static_cast<double>(K) * static_cast<double>(H) *
                static_cast<double>(W)) +
               (static_cast<double>(N) * static_cast<double>(C) + static_cast<double>(C) * static_cast<double>(K) +
                static_cast<double>(N) * static_cast<double>(K)) *
                   (static_cast<double>(H) * static_cast<double>(W)) *
                   std::log2(static_cast<double>(H) * static_cast<double>(W));
      case CUDNN_CONVOLUTION_BWD_DATA_ALGO_WINOGRAD:
      case CUDNN_CONVOLUTION_BWD_DATA_ALGO_WINOGRAD_NONFUSED:
        return static_cast<double>(-1); // todo ... implement
      case CUDNN_CONVOLUTION_BWD_DATA_ALGO_COUNT:
      default:
        return static_cast<double>(-1);
    }
  };

  const double predicted_flops = compute_flops(convolution_algorithm) / group;
  state.counters.insert(
      {{"predicted_flops_count", predicted_flops},
       {"predicted_flops", {predicted_flops * state.iterations(), benchmark::Counter::kAvgThreadsRate}}});

  if (advised_convolution_algorithm != -1) {
    const double predicted_advised_flops = compute_flops(advised_convolution_algorithm);
    state.counters.insert({{"predicted_advised_flops_count", predicted_advised_flops},
                           {"predicted_advised_flops",
                            {predicted_advised_flops * state.iterations(), benchmark::Counter::kAvgThreadsRate}}});
  }

  static const int max_count = 10;
  /* cudnn_err = cudnnGetConvolutionBackwardDataAlgorithmMaxCount(cudnn_handle, &max_count); */
  /* if (PRINT_IF_ERROR(cudnn_err)) { */
  /*   state.SkipWithError(BENCHMARK_NAME " failed to perform cudnnGetConvolutionBackwardDataAlgorithmMaxCount"); */
  /* } */

  cudnnConvolutionBwdDataAlgoPerf_t perfResults[max_count];
  int returned_count;
  cudnn_err =
      cudnnFindConvolutionBackwardDataAlgorithm(cudnn_handle, w_descriptor, dy_descriptor, convolution_descriptor,
                                                dx_descriptor, max_count, &returned_count, perfResults);
  if (PRINT_IF_ERROR(cudnn_err)) {
    state.SkipWithError(BENCHMARK_NAME " failed to perform cudnnFindConvolutionBackwardDataAlgorithm");
  }

  for (auto ii = 0; ii < returned_count; ii++) {
    cudnnConvolutionBwdDataAlgoPerf_t perfResult = perfResults[ii];
    if (perfResult.algo == convolution_algorithm) {
      state.counters.insert({{"advised_time", perfResult.time},
                             {"advised_memory", perfResult.memory},
                             {"advised_determinism", (int) perfResult.determinism}});
    }
  }

  state.SetItemsProcessed(int64_t(state.iterations()) * N * K * C * W * H);
}

template <typename T, cudnnConvolutionBwdDataAlgo_t convolution_algorithm
#ifdef CUDNN_SUPPORTS_TENSOR_OPS
          ,
          cudnnMathType_t math_type = CUDNN_DEFAULT_MATH
#endif // CUDNN_SUPPORTS_TENSOR_OPS
          >
static void LAYER_CUDNN_CONV_BWD_DATA_Impl(benchmark::State& state) {

  try {
    iLAYER_CUDNN_CONV_BWD_DATA_Impl<T, convolution_algorithm
#ifdef CUDNN_SUPPORTS_TENSOR_OPS
                                    ,
                                    math_type
#endif // CUDNN_SUPPORTS_TENSOR_OPS
                                    >(state);
  } catch (const std::exception& e) {
    const auto err = std::string("Exception in " BENCHMARK_NAME) + e.what();
    state.SkipWithError(err.c_str());
  } catch (const std::string& e) {
    const auto err = std::string("Exception in " BENCHMARK_NAME) + e;
    state.SkipWithError(err.c_str());
  } catch (...) {
    state.SkipWithError("unknown exception in " BENCHMARK_NAME);
  }
}

#ifdef GENERATED_BENCHMARK_LAYER

#define ENABLE_LAYER_CUDNN_CONV_BWD_DATA 1

#include "generated_benchmarks.hpp"

#undef ENABLE_LAYER_CUDNN_CONV_BWD_DATAS

#else // GENERATED_BENCHMARK_LAYER

template <cudnnConvolutionBwdDataAlgo_t convolution_algorithm>
static void LAYER_CUDNN_CONV_BWD_DATA_INT8(benchmark::State& state) {
  LAYER_CUDNN_CONV_BWD_DATA_Impl<int8_t, convolution_algorithm>(state);
}

template <cudnnConvolutionBwdDataAlgo_t convolution_algorithm>
static void LAYER_CUDNN_CONV_BWD_DATA_INT32(benchmark::State& state) {
  LAYER_CUDNN_CONV_BWD_DATA_Impl<int32_t, convolution_algorithm>(state);
}

template <cudnnConvolutionBwdDataAlgo_t convolution_algorithm>
static void LAYER_CUDNN_CONV_BWD_DATA_HALF(benchmark::State& state) {
  LAYER_CUDNN_CONV_BWD_DATA_Impl<__half, convolution_algorithm>(state);
}

#ifdef CUDNN_SUPPORTS_TENSOR_OPS
template <cudnnConvolutionBwdDataAlgo_t convolution_algorithm>
static void LAYER_CUDNN_CONV_BWD_DATA_HALF_TENSOROP(benchmark::State& state) {
  LAYER_CUDNN_CONV_BWD_DATA_Impl<__half, convolution_algorithm, CUDNN_TENSOR_OP_MATH>(state);
}
#endif

template <cudnnConvolutionBwdDataAlgo_t convolution_algorithm>
static void LAYER_CUDNN_CONV_BWD_DATA_FLOAT(benchmark::State& state) {
  LAYER_CUDNN_CONV_BWD_DATA_Impl<float, convolution_algorithm>(state);
}

template <cudnnConvolutionBwdDataAlgo_t convolution_algorithm>
static void LAYER_CUDNN_CONV_BWD_DATA_DOUBLE(benchmark::State& state) {
  LAYER_CUDNN_CONV_BWD_DATA_Impl<double, convolution_algorithm>(state);
}

#define CONV_PROBLEMS ALL_INFERENCE_SERVER_CONV_PROBLEMS

#define BENCHMARK_LAYER(b)                                                                                             \
  BENCHMARK_CUDNN_TEMPLATE(b, CUDNN_CONVOLUTION_BWD_DATA_ALGO_0)->INFERENCE_SERVER_CONV_PROBLEMS()->UseManualTime();   \
  BENCHMARK_CUDNN_TEMPLATE(b, CUDNN_CONVOLUTION_BWD_DATA_ALGO_1)->INFERENCE_SERVER_CONV_PROBLEMS()->UseManualTime();   \
  BENCHMARK_CUDNN_TEMPLATE(b, CUDNN_CONVOLUTION_BWD_DATA_ALGO_FFT)->INFERENCE_SERVER_CONV_PROBLEMS()->UseManualTime(); \
  BENCHMARK_CUDNN_TEMPLATE(b, CUDNN_CONVOLUTION_BWD_DATA_ALGO_FFT_TILING)                                              \
      ->INFERENCE_SERVER_CONV_PROBLEMS()                                                                               \
      ->UseManualTime();                                                                                               \
  BENCHMARK_CUDNN_TEMPLATE(b, CUDNN_CONVOLUTION_BWD_DATA_ALGO_WINOGRAD)                                                \
      ->INFERENCE_SERVER_CONV_PROBLEMS()                                                                               \
      ->UseManualTime();                                                                                               \
  BENCHMARK_CUDNN_TEMPLATE(b, CUDNN_CONVOLUTION_BWD_DATA_ALGO_WINOGRAD_NONFUSED)                                       \
      ->INFERENCE_SERVER_CONV_PROBLEMS()                                                                               \
      ->UseManualTime()

// BENCHMARK_LAYER(LAYER_CUDNN_CONV_BWD_DATA_INT8);
// BENCHMARK_LAYER(LAYER_CUDNN_CONV_BWD_DATA_INT32);
BENCHMARK_LAYER(LAYER_CUDNN_CONV_BWD_DATA_HALF);
#ifdef CUDNN_SUPPORTS_TENSOR_OPS
BENCHMARK_LAYER(LAYER_CUDNN_CONV_BWD_DATA_HALF_TENSOROP);
#endif // CUDNN_SUPPORTS_TENSOR_OPS
BENCHMARK_LAYER(LAYER_CUDNN_CONV_BWD_DATA_FLOAT);
// BENCHMARK_LAYER(LAYER_CUDNN_CONV_BWD_DATA_DOUBLE);

#endif // GENERATED_BENCHMARK_LAYER
