#!/bin/bash

# https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/optimize_gpu.html
for GPU in $(nvidia-smi --format=csv,noheader --query-gpu=uuid); do
  sudo nvidia-smi -rac -i $GPU
  MEMclock=$(nvidia-smi -q -i 0 -d CLOCK | grep -A2 Default\ Applications\ Clocks | grep Memory | tr -d \  | cut -d: -f2 | sed s/MHz//g | sort -nr | head -n1)
  SMclock=$(nvidia-smi -q -i 0 -d CLOCK | grep -A2 Default\ Applications\ Clocks | grep Graphics | tr -d \  | cut -d: -f2 | sed s/MHz//g | sort -nr | head -n1)
  sudo nvidia-smi -pm ENABLED -i $GPU
  sudo nvidia-smi -ac $MEMclock,$SMclock -i $GPU
  sudo nvidia-smi --auto-boost-default=DISABLED -i $GPU
done