# cuDNN|Scope

Benchmark cuDNN for different DL layers.

## Contributors

* [Cheng Li](mailto:cli99@illinois.edu)
* [Abdul Dakkak](mailto:dakkak@illinois.edu)

## Documentation

See the `docs` folder for a description of the benchmarks.

## Build / Run

```
./scripts/run_benchmarks.sh
```

You need a recent CMAKE which can be downloaded from https://apt.kitware.com/

## Build / Run with CUPTI Profiling

```
./scripts/run_benchmarks_cupti.sh
```

## Usage

### Use predefined parameters to generate the benchmarks

The parameters for the convolution benchmarks are taken from Baidu Research [DeepBench](https://github.com/baidu-research/DeepBench/blob/master/code/kernels/conv_problems.h).
Benchmarks for the other layer types also use these parameters for now.

### Use model-defined parameters to generate the benchmarks

GENERATED_BENCHMARK_LAYER

## Customize

To update sugar files for compilation, run
```
 python $SCOPE_PATH/tools/generate_sugar_files.py --top $SCOPE_PATH/scopes/cudnn_scope/src/
```
## cuDNN API for DL layers

Inputs to layers are either [arg.hpp](https://github.com/c3sr/cudnn_scope/blob/master/src/args.hpp) or derived form it. The arguments are from the [convolution in Baidu DeepBench](https://github.com/baidu-research/DeepBench/blob/master/code/kernels/conv_problems.h)

### Activation

* [CUDNN_ACTIVATION_BWD](src/activation_bwd.cpp)
* [CUDNN_ACTIVATION_FWD](src/activation_fwd.cpp)

[cudnnActivationBackward](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnActivationBackward)

[cudnnActivationForward](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnActivationForward)

[cudnnActivationMode_t](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnActivationMode_t)
[cudnnSetActivationDescriptor](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnSetActivationDescriptor)

### Batchnorm

* [CUDNN_BATCHNORM_BWD](src/batchnorm_bwd.cpp)
* [CUDNN_BATCHNORM_FWD_INFERENCE](batchnorm_fwd.cpp)
* [CUDNN_BATCHNORM_FWD_TRAINING](batchnorm_fwd.cpp)

[cudnnBatchNormalizationBackward](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnBatchNormalizationBackward)

[cudnnBatchNormalizationForwardTraining](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnBatchNormalizationForwardTraining)

[cudnnBatchNormalizationForwardInference](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnBatchNormalizationForwardInference)

[cudnnDeriveBNTensorDescriptor](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnDeriveBNTensorDescriptor)
[cudnnBatchNormMode_t](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnBatchNormMode_t)

### Convoluation

* [CUDNN_CONV_BIAS_ACTIVATION_FWD](src/conv_bias_activation_fwd.cpp)
* [CUDNN_CONV_BWD_BIAS](conv_bwd_bias.cpp)
* [CUDNN_CONV_BWD_DATA](conv_bwd_data.cpp)
* [CUDNN_CONV_BWD_FILTER](conv_bwd_filter.cpp)
* [CUDNN_CONV_FWD](src/conv_fwd.cpp)

[cudnnConvolutionBiasActivationForward](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnConvolutionBiasActivationForward)

[cudnnConvolutionBackwardBias](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnConvolutionBackwardBias)

[cudnnConvolutionBackwardData](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnConvolutionBackwardData)

[cudnnConvolutionBackwardFilter](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnConvolutionBackwardFilter)

[cudnnConvolutionForward](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnConvolutionForward)

[cudnnConvolutionBwdDataAlgo_t](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnConvolutionBwdDataAlgo_t)
[cudnnConvolutionBwdFilterAlgo_t](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnConvolutionBwdFilterAlgo_t)
[cudnnConvolutionFwdAlgo_t](http://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnConvolutionFwdAlgo_t)
[cudnnSetConvolution2dDescriptor](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnSetConvolution2dDescriptor)
[cudnnGetConvolution2dForwardOutputDim](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnGetConvolution2dForwardOutputDim)
[cudnnGetConvolutionForwardWorkspaceSize](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnGetConvolutionForwardWorkspaceSize)

### Dropout

* [CUDNN_DROPOUT_BWD](src/dropout_bwd.cpp)
* [CUDNN_DROPOUT_FWD](src/dropout_fwd.cpp)

[cudnnDropoutBackward](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnDropoutBackward)

[cudnnDropoutForward](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnDropoutForward)

[cudnnDropoutGetStatesSize](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnDropoutGetStatesSize)
[cudnnSetDropoutDescriptor](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnSetDropoutDescriptor)
[cudnnDropoutGetReserveSpaceSize](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnDropoutGetReserveSpaceSize)

### Pooling

* [CUDNN_POOLING_BWD](src/pooling_bwd.cpp)
* [CUDNN_POOLING_FWD](src/pooling_fwd.cpp)

[cudnnPoolingBackward](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnPoolingBackward)

[cudnnPoolingForward](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnPoolingForward)

[cudnnGetPooling2dForwardOutputDim](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnGetPooling2dForwardOutputDim)
[cudnnSetPooling2dDescriptor](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnSetPooling2dDescriptor)

### Softmax

* [CUDNN_SOFTMAX_BWD](src/softmax_bwd.cpp)
* [CUDNN_SOFTMAX_FWD](src/softmax_fwd.cpp)

[cudnnSoftmaxBackward](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnSoftmaxBackward)

[cudnnSoftmaxForward](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnSoftmaxForward)

[cudnnSoftmaxMode_t](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnSoftmaxMode_t)
[cudnnSoftmaxAlgorithm_t](https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnSoftmaxAlgorithm_t)
