#define BENCHMARK_NAME "CUDNN/CONV_BIAS_ACTIVATION_FWD"

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

// https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnConvolutionBiasActivationForward
// https://github.com/tensorflow/tensorflow/blob/575db18083d5437fd7a87ccf3414303498911bb1/tensorflow/stream_executor/cuda/cuda_dnn.cc#L2547
template <typename T, cudnnConvolutionFwdAlgo_t convolution_algorithm, cudnnActivationMode_t activation_mode
#ifdef CUDNN_SUPPORTS_TENSOR_OPS
          ,
          cudnnMathType_t math_type0 = CUDNN_DEFAULT_MATH
#endif // CUDNN_SUPPORTS_TENSOR_OPS
          >
static void iLAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_Impl(benchmark::State& state) {
  if (!has_cuda) {
    state.SkipWithError(BENCHMARK_NAME " no CUDA device found");
    return;
  }
#ifdef CUDNN_SUPPORTS_TENSOR_OPS
  int math_type = CUDNN_DEFAULT_MATH;
  if ((is_half_v<T> || math_type == CUDNN_TENSOR_OP_MATH) && !detail::SupportsTensorCore(cuda_device_id)) {
    state.SkipWithError(BENCHMARK_NAME "no Tensorcore support on current device");
    return;
  }
  if (is_half_v<T>) {
    math_type = CUDNN_TENSOR_OP_MATH;
  }
#else
  int math_type = 0;
#endif // CUDNN_SUPPORTS_TENSOR_OPS

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

  MEM_ALIGNED_128 const T alpha          = detail::one<T>();
  MEM_ALIGNED_128 const T beta           = detail::zero<T>();
  const double coef                      = 1;
  const cudnnConvolutionMode_t conv_mode = CUDNN_CROSS_CORRELATION; // CUDNN_CONVOLUTION;

  static constexpr auto layout = 
#ifdef LOW_PRECISION_NHWC_MODE
      (std::is_integral<T>::value || is_half_v<T> ? Layout::NHWC : Layout::NCHW);
#else  // LOW_PRECISION_NHWC_MODE
      (std::is_integral<T>::value ? Layout::NHWC : Layout::NCHW);
#endif // LOW_PRECISION_NHWC_MODE

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

  MEM_ALIGNED_128 cudnnActivationDescriptor_t activation_descriptor;
  if (PRINT_IF_ERROR(cudnnCreateActivationDescriptor(&activation_descriptor))) {
    state.SkipWithError(BENCHMARK_NAME " failed to cudnnCreateActivationDescriptor");
    return;
  }

  if (PRINT_IF_ERROR(
          cudnnSetActivationDescriptor(activation_descriptor, activation_mode, CUDNN_NOT_PROPAGATE_NAN, coef))) {
    state.SkipWithError(BENCHMARK_NAME " failed to cudnnSetActivationDescriptor");
    return;
  }
  defer(cudnnDestroyActivationDescriptor(activation_descriptor));

  if (group > 0 && PRINT_IF_ERROR(cudnnSetConvolutionGroupCount(convolution_descriptor, group))) {
    state.SkipWithError(BENCHMARK_NAME " failed to cudnnSetConvolutionGroupCount");
    return;
  }

#ifdef CUDNN_SUPPORTS_TENSOR_OPS
  cudnnSetConvolutionMathType(convolution_descriptor, (cudnnMathType_t) math_type);
#endif // CUDNN_SUPPORTS_TENSOR_OPS

  MEM_ALIGNED_128 auto x_tensor = Tensor<T, layout>(state,
                                            {/*batch_size=*/batch_size,
                                             /*channels=*/channels,
                                             /*image_height=*/height,
                                             /*image_width=*/width});
  if (!x_tensor.is_valid) {
    return;
  }
  MEM_ALIGNED_128 cudnnTensorDescriptor_t x_descriptor = x_tensor.get();

  MEM_ALIGNED_128 const auto w_filter = Filter<T, layout>(state,
                                                  {/*out_channels=*/num_filters,
                                                   /*in_channels=*/channels,
                                                   /*kernel_height=*/filter_height,
                                                   /*kernel_width=*/filter_width},
                                                  group);
  if (!w_filter.is_valid) {
    return;
  }
  MEM_ALIGNED_128 cudnnFilterDescriptor_t w_descriptor = w_filter.get();

  int out_n, out_c, out_h, out_w;
  const auto cudnn_get_conv_output_err = cudnnGetConvolution2dForwardOutputDim(
      convolution_descriptor, x_descriptor, w_descriptor, &out_n, &out_c, &out_h, &out_w);
  if (PRINT_IF_ERROR(cudnn_get_conv_output_err)) {
    state.SkipWithError(fmt::format(BENCHMARK_NAME " failed to cudnnGetConvolution2dForwardOutputDim because of {}",
                                    utils::detail::error_string(cudnn_get_conv_output_err))
                            .c_str());
    return;
  }

  MEM_ALIGNED_128 auto y_tensor = Tensor<T, layout>(state,
                                            {/*batch_size=*/out_n,
                                             /*channels=*/out_c,
                                             /*image_height=*/out_h,
                                             /*image_width=*/out_w});
  if (!y_tensor.is_valid) {
    return;
  }
  MEM_ALIGNED_128 cudnnTensorDescriptor_t y_descriptor = y_tensor.get();

  MEM_ALIGNED_128 auto bias_tensor = Tensor<T, layout>(state,
                                               {/*batch_size=*/1,
                                                /*channels=*/out_c,
                                                /*image_height=*/1,
                                                /*image_width=*/1});
  if (!bias_tensor.is_valid) {
    return;
  }
  MEM_ALIGNED_128 cudnnTensorDescriptor_t bias_descriptor = bias_tensor.get();

  MEM_ALIGNED_128 cudnnConvolutionFwdAlgo_t advised_convolution_algorithm = (cudnnConvolutionFwdAlgo_t) -1;
  if (cudnnGetConvolutionForwardAlgorithm(cudnn_handle, x_descriptor, w_descriptor, convolution_descriptor,
                                          y_descriptor, CUDNN_CONVOLUTION_FWD_PREFER_FASTEST, 0,
                                          &advised_convolution_algorithm) != CUDNN_STATUS_SUCCESS) {
    advised_convolution_algorithm = (cudnnConvolutionFwdAlgo_t) -1;
  }

  MEM_ALIGNED_128 size_t workspace_bytes = 0;
  if (std::is_same<T, int8_t>::value) {

    // Note: cudnn workspace size function doesn't work for INT8_CONFIG
    workspace_bytes = 1073741824;
  } else {
    if (PRINT_IF_ERROR(cudnnGetConvolutionForwardWorkspaceSize(cudnn_handle,
                                                               x_descriptor,
                                                               w_descriptor,
                                                               convolution_descriptor,
                                                               y_descriptor,
                                                               convolution_algorithm,
                                                               &workspace_bytes))) {
      workspace_bytes = 1073741824;
      // state.SkipWithError(BENCHMARK_NAME " failed to cudnnGetConvolutionForwardWorkspaceSize");
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

  const auto bias_bytes = sizeof(T) * 1 * out_c * 1 * 1;
  auto bias             = std::vector<T>(bias_bytes / sizeof(T));
  std::fill(bias.begin(), bias.end(), detail::one<T>());

  MEM_ALIGNED_128 DeviceMemory<T> workspace_memory(state, workspace_bytes);
  if (!workspace_memory.is_valid) {
    return;
  }
  MEM_ALIGNED_128 const auto d_workspace = workspace_memory.get();

  MEM_ALIGNED_128 DeviceMemory<T> x_memory(state, input.data(), input_bytes);
  if (!x_memory.is_valid) {
    return;
  }
  MEM_ALIGNED_128 const auto d_x = x_memory.get();

  MEM_ALIGNED_128 DeviceMemory<T> w_memory(state, kernel.data(), kernel_bytes);
  if (!w_memory.is_valid) {
    return;
  }
  MEM_ALIGNED_128 const auto d_w = w_memory.get();

  MEM_ALIGNED_128 DeviceMemory<T> y_memory(state, output_bytes);
  if (!y_memory.is_valid) {
    return;
  }
  MEM_ALIGNED_128 const auto d_y = y_memory.get();

  MEM_ALIGNED_128 DeviceMemory<T> z_memory(state, output.data(), output_bytes);
  if (!z_memory.is_valid) {
    return;
  }
  MEM_ALIGNED_128 const auto d_z = z_memory.get();

  MEM_ALIGNED_128 DeviceMemory<T> bias_memory(state, bias.data(), bias_bytes);
  if (!bias_memory.is_valid) {
    return;
  }
  MEM_ALIGNED_128 const auto d_bias = bias_memory.get();

  cudnnStatus_t cudnn_err;
  BENCHMARK_BLOCK(cudnn_err, {
    cudnn_err = cudnnConvolutionBiasActivationForward(cudnn_handle, &alpha, x_descriptor, d_x, w_descriptor, d_w,
                                                      convolution_descriptor, convolution_algorithm, d_workspace,
                                                      workspace_bytes, &beta, y_descriptor, d_z, bias_descriptor,
                                                      d_bias, activation_descriptor, y_descriptor, d_y);
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
                         {"conv_mode", (int) conv_mode},
                         {"workspace_bytes", workspace_bytes},
                         {"workspace_megabytes", workspace_bytes / 1048576.0},
                         {"convolution_algorithm", (int) convolution_algorithm},
                         {"advised_convolution_algorithm", (int) advised_convolution_algorithm},
                         {"math_type", (int) math_type},
                         {"x_tensor_layout", (int) x_tensor.layout},
                         {"y_tensor_layout", (int) y_tensor.layout},
                         {"bias_tensor_layout", (int) bias_tensor.layout},
                         {"w_filter_layout", (int) w_filter.layout},
                         {"activation_mode", (int) activation_mode}});

  static const int max_count = 20;
  /* cudnn_err = cudnnGetConvolutionForwardAlgorithmMaxCount(cudnn_handle, &max_count); */
  /* if (PRINT_IF_ERROR(cudnn_err)) { */
  /*   state.SkipWithError(BENCHMARK_NAME " failed to perform cudnnGetConvolutionForwardAlgorithmMaxCount"); */
  /* } */

  cudnnConvolutionFwdAlgoPerf_t perfResults[max_count];
  int returned_count;
  cudnn_err = cudnnFindConvolutionForwardAlgorithm(cudnn_handle, x_descriptor, w_descriptor, convolution_descriptor,
                                                   y_descriptor, max_count, &returned_count, perfResults);
  if (PRINT_IF_ERROR(cudnn_err)) {
    state.SkipWithError(BENCHMARK_NAME " failed to perform cudnnFindConvolutionForwardAlgorithm");
  }

  for (auto ii = 0; ii < returned_count; ii++) {
    cudnnConvolutionFwdAlgoPerf_t perfResult = perfResults[ii];
    if (perfResult.algo == convolution_algorithm) {
      state.counters.insert({{"advised_time", perfResult.time},
                             {"advised_memory", perfResult.memory},
                             {"advised_determinism", (int) perfResult.determinism}});
    }
  }

  const auto N = batch_size, K = num_filters, C = channels, H = height, W = width, R = filter_height, S = filter_width;

  state.SetItemsProcessed(int64_t(state.iterations()) * N * K * C * W * H);
}

template <typename T, cudnnConvolutionFwdAlgo_t convolution_algorithm, cudnnActivationMode_t activation_mode
#ifdef CUDNN_SUPPORTS_TENSOR_OPS
          ,
          cudnnMathType_t math_type = CUDNN_DEFAULT_MATH
#endif // CUDNN_SUPPORTS_TENSOR_OPS
          >
static void LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_Impl(benchmark::State& state) {
  try {
    iLAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_Impl<T, convolution_algorithm, activation_mode
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

#define LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_0_Impl LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_Impl
#define LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_1_Impl LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_Impl
#define LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_2_Impl LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_Impl
#define LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_3_Impl LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_Impl
#define LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_4_Impl LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_Impl
#define LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_5_Impl LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_Impl
#define LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_6_Impl LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_Impl
#define LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_7_Impl LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_Impl
#define LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_8_Impl LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_Impl
#define LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_9_Impl LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_Impl

#ifdef GENERATED_BENCHMARK_LAYER

/* #define ENABLE_LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD 1 */

#define FUSED_ONLY_BENCHMARKS 1
#include "generated_benchmarks.hpp"
#undef FUSED_ONLY_BENCHMARKS

/* #undef ENABLE_LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD */

#else // GENERATED_BENCHMARK_LAYER

template <cudnnConvolutionFwdAlgo_t convolution_algorithm, cudnnActivationMode_t activation_mode>
static void LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_INT8(benchmark::State& state) {
  LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_Impl<int8_t, convolution_algorithm, activation_mode>(state);
}

template <cudnnConvolutionFwdAlgo_t convolution_algorithm, cudnnActivationMode_t activation_mode>
static void LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_INT32(benchmark::State& state) {
  LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_Impl<int32_t, convolution_algorithm, activation_mode>(state);
}

template <cudnnConvolutionFwdAlgo_t convolution_algorithm, cudnnActivationMode_t activation_mode>
static void LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_HALF(benchmark::State& state) {
  LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_Impl<__half, convolution_algorithm, activation_mode>(state);
}

#ifdef CUDNN_SUPPORTS_TENSOR_OPS
template <cudnnConvolutionFwdAlgo_t convolution_algorithm, cudnnActivationMode_t activation_mode>
static void LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_HALF_TENSOROP(benchmark::State& state) {
  LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_Impl<__half, convolution_algorithm, activation_mode, CUDNN_TENSOR_OP_MATH>(
      state);
}
#endif

template <cudnnConvolutionFwdAlgo_t convolution_algorithm, cudnnActivationMode_t activation_mode>
static void LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_FLOAT(benchmark::State& state) {
  LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_Impl<float, convolution_algorithm, activation_mode>(state);
}

template <cudnnConvolutionFwdAlgo_t convolution_algorithm, cudnnActivationMode_t activation_mode>
static void LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_DOUBLE(benchmark::State& state) {
  LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_Impl<double, convolution_algorithm, activation_mode>(state);
}

#define CONV_PROBLEMS INFERENCE_SERVER_CONV_PROBLEMS

#define BENCHMARK_CUDNN0(b, ACTIVATION_MODE)                                                                           \
  BENCHMARK_CUDNN_TEMPLATE(b, CUDNN_CONVOLUTION_FWD_ALGO_IMPLICIT_GEMM, ACTIVATION_MODE)                               \
      ->CONV_PROBLEMS()                                                                                                \
      ->UseManualTime();                                                                                               \
  BENCHMARK_CUDNN_TEMPLATE(b, CUDNN_CONVOLUTION_FWD_ALGO_IMPLICIT_PRECOMP_GEMM, ACTIVATION_MODE)                       \
      ->CONV_PROBLEMS()                                                                                                \
      ->UseManualTime();                                                                                               \
  BENCHMARK_CUDNN_TEMPLATE(b, CUDNN_CONVOLUTION_FWD_ALGO_GEMM, ACTIVATION_MODE)->CONV_PROBLEMS()->UseManualTime();     \
  BENCHMARK_CUDNN_TEMPLATE(b, CUDNN_CONVOLUTION_FWD_ALGO_DIRECT, ACTIVATION_MODE)->CONV_PROBLEMS()->UseManualTime();   \
  BENCHMARK_CUDNN_TEMPLATE(b, CUDNN_CONVOLUTION_FWD_ALGO_FFT, ACTIVATION_MODE)->CONV_PROBLEMS()->UseManualTime();      \
  BENCHMARK_CUDNN_TEMPLATE(b, CUDNN_CONVOLUTION_FWD_ALGO_FFT_TILING, ACTIVATION_MODE)                                  \
      ->CONV_PROBLEMS()                                                                                                \
      ->UseManualTime();                                                                                               \
  BENCHMARK_CUDNN_TEMPLATE(b, CUDNN_CONVOLUTION_FWD_ALGO_WINOGRAD, ACTIVATION_MODE)->CONV_PROBLEMS()->UseManualTime(); \
  BENCHMARK_CUDNN_TEMPLATE(b, CUDNN_CONVOLUTION_FWD_ALGO_WINOGRAD_NONFUSED, ACTIVATION_MODE)                           \
      ->CONV_PROBLEMS()                                                                                                \
      ->UseManualTime()

#define BENCHMARK_LAYER(b) BENCHMARK_CUDNN0(b, CUDNN_ACTIVATION_RELU)

/* BENCHMARK_LAYER(LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_INT8); */
/* BENCHMARK_LAYER(LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_INT32); */
BENCHMARK_LAYER(LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_HALF);
#ifdef CUDNN_SUPPORTS_TENSOR_OPS
BENCHMARK_LAYER(LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_HALF_TENSOROP);
#endif // CUDNN_SUPPORTS_TENSOR_OPS
BENCHMARK_LAYER(LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_FLOAT);
// BENCHMARK_LAYER(LAYER_CUDNN_CONV_BIAS_ACTIVATION_FWD_DOUBLE);

#endif // GENERATED_BENCHMARK_LAYER
