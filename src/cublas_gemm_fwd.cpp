#define BENCHMARK_NAME "CUBLAS/GEMM_FWD"
#define IMPLEMENTATION_NAME BENCHMARK_NAME

#include <benchmark/benchmark.h>

#include <cmath>
#include <iostream>
#include <mutex>
#include <numeric>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

#include <cublas_v2.h>
#include <cuda_runtime.h>

#include "args.hpp"
#include "error.hpp"
#include "helper.hpp"
#include "init.hpp"
#include "utils.hpp"

// ONLY SUPPORT SGEMM FOR NOW

static void constantInit(float* data, int size, float val) {
  for (int i = 0; i < size; ++i) {
    data[i] = val;
  }
}

// https://docs.nvidia.com/cuda/cublas/index.html#cublas-lt-t-gt-gemm
template <typename T>
static void iLAYER_CUBLAS_GEMM_FWD_Impl(benchmark::State& state) {
  if (!has_cuda) {
    state.SkipWithError(BENCHMARK_NAME " no CUDA device found");
    return;
  }

  // n, c, h, w
  const auto M                   = state.range(0);
  const auto N                   = state.range(1);
  const auto K                   = state.range(2);
  const cublasOperation_t transA = state.range(3) == 0 ? CUBLAS_OP_N : CUBLAS_OP_T;
  const cublasOperation_t transB = state.range(4) == 0 ? CUBLAS_OP_N : CUBLAS_OP_T;
  auto alpha                     = state.range(5);
  auto beta                      = state.range(6);

  const int lda = (transA == CUBLAS_OP_N) ? M : K;
  const int ldb = (transB == CUBLAS_OP_N) ? K : N;

  state.counters.insert({
      {"M", M},
      {"N", N},
      {"K", K},
      {"alpha", alpha},
      {"beta", beta},
      {"lda", lda},
      {"ldb", ldb},
      {"transA", transA == CUBLAS_OP_N ? 0 : 1},
      {"transB", transB == CUBLAS_OP_N ? 0 : 1},
  });

  const T one  = detail::one<T>();
  const T zero = detail::zero<T>();

  auto a = std::vector<T>(M * K);
  auto b = std::vector<T>(K * N);
  auto c = std::vector<T>(M * N);

  std::fill(a.begin(), a.end(), one);
  std::fill(b.begin(), b.end(), one);
  std::fill(c.begin(), c.end(), zero);

  if constexpr (is_half_v<T>) {
    if (PRINT_IF_ERROR(cublasSetMathMode(cublas_handle, CUBLAS_TENSOR_OP_MATH))) {
      LOG(critical, "CUBLAS/{} failed to sett math mode to default", IMPLEMENTATION_NAME);
      state.SkipWithError(fmt::format("CUBLAS/{} failed to set math mode to defaultt", IMPLEMENTATION_NAME).c_str());
      return;
    }
  } else {
    if (PRINT_IF_ERROR(cublasSetMathMode(cublas_handle, CUBLAS_DEFAULT_MATH))) {
      LOG(critical, "CUBLAS/{} failed to sett math mode to default", IMPLEMENTATION_NAME);
      state.SkipWithError(fmt::format("CUBLAS/{} failed to set math mode to defaultt", IMPLEMENTATION_NAME).c_str());
      return;
    }
  }

  T *d_a{nullptr}, *d_b{nullptr}, *d_c{nullptr};

  if (PRINT_IF_ERROR(cudaMalloc((void**) &d_a, a.size() * sizeof(*a.data())))) {
    LOG(critical, "CUBLAS/{} device memory allocation failed for matrix A", IMPLEMENTATION_NAME);
    state.SkipWithError(
        fmt::format("CUBLAS/{} device memory allocation failed for matrix A", IMPLEMENTATION_NAME).c_str());
    return;
  }
  defer(cudaFree(d_a));

  if (PRINT_IF_ERROR(cudaMalloc((void**) &d_b, b.size() * sizeof(*b.data())))) {
    LOG(critical, "CUBLAS/{} device memory allocation failed for matrix B", IMPLEMENTATION_NAME);
    state.SkipWithError(
        fmt::format("CUBLAS/{} device memory allocation failed for matrix B", IMPLEMENTATION_NAME).c_str());
    return;
  }
  defer(cudaFree(d_b));

  if (PRINT_IF_ERROR(cudaMalloc((void**) &d_c, c.size() * sizeof(*c.data())))) {
    LOG(critical, "CUBLAS/{} device memory allocation failed for matrix C", IMPLEMENTATION_NAME);
    state.SkipWithError(
        fmt::format("CUBLAS/{} device memory allocation failed for matrix C", IMPLEMENTATION_NAME).c_str());
    return;
  }
  defer(cudaFree(d_c));

  if (PRINT_IF_ERROR(cublasSetMatrix(M, K, sizeof(*a.data()), a.data(), M, d_a, M))) {
    LOG(critical, "CUBLAS/{} setting of A matrix failed", IMPLEMENTATION_NAME);
    state.SkipWithError(fmt::format("CUBLAS/{} setting of A matrix failed", IMPLEMENTATION_NAME).c_str());
    return;
  }

  if (PRINT_IF_ERROR(cublasSetMatrix(K, N, sizeof(*b.data()), b.data(), K, d_b, K))) {
    LOG(critical, "CUBLAS/{} setting of B matrix failed", IMPLEMENTATION_NAME);
    state.SkipWithError(fmt::format("CUBLAS/{} setting of B matrix failed", IMPLEMENTATION_NAME).c_str());
    return;
  }

  if (PRINT_IF_ERROR(cublasSetMatrix(M, N, sizeof(*c.data()), c.data(), M, d_c, M))) {
    LOG(critical, "CUBLAS/{} setting of C matrix failed", IMPLEMENTATION_NAME);
    state.SkipWithError(fmt::format("CUBLAS/{} setting of C matrix failed", IMPLEMENTATION_NAME).c_str());
    return;
  }

  cublasStatus_t cublas_err;
  if constexpr (is_half_v<T>) {
    BENCHMARK_BLOCK(cublas_err, {
      cublas_err = cublasGemmEx(cublas_handle, CUBLAS_OP_N, CUBLAS_OP_N, M, N, K, &alpha, d_a, CUDA_R_16F, M, d_b,
                                CUDA_R_16F, K, &beta, d_c, CUDA_R_16F, M, CUDA_R_16F, CUBLAS_GEMM_DEFAULT_TENSOR_OP);
    });
  } else {
    BENCHMARK_BLOCK(cublas_err, {
      cublas_err = cublasSgemm(cublas_handle, transA, transB, M, N, K, reinterpret_cast<T*>(&alpha), d_a, lda, d_b, ldb,
                               reinterpret_cast<T*>(&beta), d_c, M);
    });
  }

  const double predicted_flops = 2.0 * M * N * K;
  state.counters.insert(
      {{"predicted_flops_count", predicted_flops},
       {"predicted_flops", {predicted_flops * state.iterations(), benchmark::Counter::kAvgThreadsRate}}});
  state.SetItemsProcessed(int64_t(state.iterations()) * M * N * K);
}

template <typename T>
static void LAYER_CUBLAS_GEMM_FWD_Impl(benchmark::State& state) {
  try {
    iLAYER_CUBLAS_GEMM_FWD_Impl<T>(state);
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

template <>
void LAYER_CUBLAS_GEMM_FWD_Impl<int8_t>(benchmark::State& state) {
  return LAYER_CUBLAS_GEMM_FWD_Impl<float>(state);
}

#ifdef GENERATED_BENCHMARK_LAYER

#define ENABLE_LAYER_CUBLAS_GEMM_FWD 1

#include "generated_benchmarks.hpp"

#undef ENABLE_LAYER_CUBLAS_GEMM_FWD

#else // GENERATED_BENCHMARK_LAYER

// static void LAYER_CUBLAS_GEMM_FWD_INT8(benchmark::State& state) {
//   LAYER_CUBLAS_GEMM_FWD_Impl<int8_t>(state);
// }

// static void LAYER_CUBLAS_GEMM_FWD_INT32(benchmark::State& state) {
//   LAYER_CUBLAS_GEMM_FWD_Impl<int32_t>(state);
// }

// static void LAYER_CUBLAS_GEMM_FWD_HALF(benchmark::State& state) {
//   LAYER_CUBLAS_GEMM_FWD_Impl<__half>(state);
// }

static void LAYER_CUBLAS_GEMM_FWD_FLOAT(benchmark::State& state) {
  LAYER_CUBLAS_GEMM_FWD_Impl<float>(state);
}

// static void LAYER_CUBLAS_GEMM_FWD_DOUBLE(benchmark::State& state) {
//   LAYER_CUBLAS_GEMM_FWD_Impl<double>(state);
// }

#define CONV_PROBLEMS INFERENCE_SERVER_CONV_PROBLEMS

/* BENCHMARK_CUDNN(LAYER_CUBLAS_GEMM_FWD_INT8)->INFERENCE_SERVER_CONV_PROBLEMS()->UseManualTime(); */
/* BENCHMARK_CUDNN(LAYER_CUBLAS_GEMM_FWD_INT32)->INFERENCE_SERVER_CONV_PROBLEMS()->UseManualTime(); */
// BENCHMARK_CUDNN(LAYER_CUBLAS_GEMM_FWD_HALF)->INFERENCE_SERVER_CONV_PROBLEMS()->UseManualTime();
BENCHMARK_CUDNN(LAYER_CUBLAS_GEMM_FWD_FLOAT)->INFERENCE_SERVER_CONV_PROBLEMS()->UseManualTime();
// BENCHMARK_CUDNN(LAYER_CUBLAS_GEMM_FWD_DOUBLE)->INFERENCE_SERVER_CONV_PROBLEMS()->UseManualTime();

#endif // GENERATED_BENCHMARK_LAYER
