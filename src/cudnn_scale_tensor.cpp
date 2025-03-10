#define BENCHMARK_NAME "CUDNN/SCALE_TENSOR"

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

// https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnAddTensor
template <typename T>
static void iLAYER_CUDNN_SCALE_TENSOR_Impl(benchmark::State& state) {
  if (!has_cuda) {
    state.SkipWithError(BENCHMARK_NAME " no CUDA device found");
    return;
  }

  const auto in_n = state.range(0);
  const auto in_c = state.range(1);
  const auto in_h = state.range(2);
  const auto in_w = state.range(3);
  const T alpha   = state.range(4);

  MEM_ALIGNED_128 auto input_tensor = Tensor<T>(state,
                                                {
                                                    in_n,
                                                    in_c,
                                                    in_h,
                                                    in_w,
                                                });
  if (!input_tensor.is_valid) {
    return;
  }
  MEM_ALIGNED_128 cudnnTensorDescriptor_t input_descriptor = input_tensor.get();

  const auto input_bytes = sizeof(T) * in_n * in_w * in_h * in_c;

  auto input = std::vector<T>(input_bytes / sizeof(T));
  std::fill(input.begin(), input.end(), detail::one<T>());

  MEM_ALIGNED_128 DeviceMemory<T> input_memory(state, input.data(), input_bytes);
  if (!input_memory.is_valid) {
    return;
  }
  MEM_ALIGNED_128 const auto d_input = input_memory.get();

  cudaEvent_t start, stop;
  PRINT_IF_ERROR(cudaEventCreate(&start));
  PRINT_IF_ERROR(cudaEventCreate(&stop));

  for (auto _ : state) {
    cudaEventRecord(start, NULL);

    const cudnnStatus_t cudnn_err = cudnnScaleTensor(cudnn_handle, input_descriptor, d_input, &alpha);

    cudaEventRecord(stop, NULL);
    const auto cuda_err = cudaEventSynchronize(stop);

    state.PauseTiming();
    if (PRINT_IF_ERROR(cudnn_err)) {
      state.SkipWithError(fmt::format(BENCHMARK_NAME " failed to perform cudnnAddTensor because of {}",
                                      utils::detail::error_string(cudnn_err))
                              .c_str());
      break;
    }
    if (PRINT_IF_ERROR(cuda_err)) {
      state.SkipWithError(fmt::format(BENCHMARK_NAME " failed to perform cudnnAddTensor because of {}",
                                      utils::detail::error_string(cuda_err))
                              .c_str());
      break;
    }

    float msecTotal = 0.0f;
    if (PRINT_IF_ERROR(cudaEventElapsedTime(&msecTotal, start, stop))) {
      state.SkipWithError(BENCHMARK_NAME " failed to launch kernel");
      break;
    }
    state.SetIterationTime(msecTotal / 1000);
    state.ResumeTiming();
  }

  state.counters.insert({{"input_size", in_n * in_c * in_h * in_w},
                         {"input_n", in_n},
                         {"input_c", in_c},
                         {"input_h", in_h},
                         {"input_w", in_w},
                         {"x_tensor_layout", (int) x_tensor.layout},
                         {"alpha", alpha}});

  state.SetItemsProcessed(int64_t(state.iterations()) * in_n * in_c * in_h * in_w);
}

template <typename T>
static void LAYER_CUDNN_SCALE_TENSOR_Impl(benchmark::State& state) {
  try {
    iLAYER_CUDNN_SCALE_TENSOR_Impl<T>(state);
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

#define ENABLE_LAYER_CUDNN_SCALE_TENSOR 1

#include "generated_benchmarks.hpp"

#undef ENABLE_LAYER_CUDNN_SCALE_TENSOR

#else // GENERATED_BENCHMARK_LAYER

static void LAYER_CUDNN_SCALE_TENSOR_INT8(benchmark::State& state) {
  LAYER_CUDNN_SCALE_TENSOR_Impl<int8_t>(state);
}

static void LAYER_CUDNN_SCALE_TENSOR_INT32(benchmark::State& state) {
  LAYER_CUDNN_SCALE_TENSOR_Impl<int32_t>(state);
}

static void LAYER_CUDNN_SCALE_TENSOR_HALF(benchmark::State& state) {
  LAYER_CUDNN_SCALE_TENSOR_Impl<__half>(state);
}

static void LAYER_CUDNN_SCALE_TENSOR_FLOAT(benchmark::State& state) {
  LAYER_CUDNN_SCALE_TENSOR_Impl<float>(state);
}

static void LAYER_CUDNN_SCALE_TENSOR_DOUBLE(benchmark::State& state) {
  LAYER_CUDNN_SCALE_TENSOR_Impl<double>(state);
}

#define CONV_PROBLEMS INFERENCE_SERVER_CONV_PROBLEMS

/* BENCHMARK_CUDNN(LAYER_CUDNN_SCALE_TENSOR_INT8)->INFERENCE_SERVER_CONV_PROBLEMS()->UseManualTime(); */
/* BENCHMARK_CUDNN(LAYER_CUDNN_SCALE_TENSOR_INT32)->INFERENCE_SERVER_CONV_PROBLEMS()->UseManualTime(); */
BENCHMARK_CUDNN(LAYER_CUDNN_SCALE_TENSOR_HALF)->INFERENCE_SERVER_CONV_PROBLEMS()->UseManualTime();
BENCHMARK_CUDNN(LAYER_CUDNN_SCALE_TENSOR_FLOAT)->INFERENCE_SERVER_CONV_PROBLEMS()->UseManualTime();
// BENCHMARK_CUDNN(LAYER_CUDNN_SCALE_TENSOR_DOUBLE)->INFERENCE_SERVER_CONV_PROBLEMS()->UseManualTime();

#endif // GENERATED_BENCHMARK_LAYER
