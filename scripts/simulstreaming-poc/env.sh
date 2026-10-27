#!/usr/bin/env bash
# Shared env for SimulStreaming POC.
# System CUDA in LD_LIBRARY_PATH (e.g. /usr/local/cuda-13.2/lib64) breaks
# pip PyTorch cuDNN ("Invalid handle. Cannot load symbol cudnnGetVersion").
# Prefer the libs shipped with the venv torch/nvidia packages.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="${ROOT}/.venv"
SITE="${VENV_DIR}/lib/python3.12/site-packages"

if [[ ! -x "${VENV_DIR}/bin/python" ]]; then
  # allow other python versions in venv
  if [[ -x "${VENV_DIR}/bin/python3" ]]; then
    :
  else
    echo "Run setup first: ${ROOT}/setup.sh" >&2
    return 1 2>/dev/null || exit 1
  fi
fi

# shellcheck disable=SC1091
source "${VENV_DIR}/bin/activate"

# Collect nvidia/*.lib and torch/lib paths from the venv.
TORCH_LIB_PATHS=()
if [[ -d "${SITE}/torch/lib" ]]; then
  TORCH_LIB_PATHS+=("${SITE}/torch/lib")
fi
if [[ -d "${SITE}/nvidia" ]]; then
  while IFS= read -r -d '' libdir; do
    TORCH_LIB_PATHS+=("${libdir}")
  done < <(find "${SITE}/nvidia" -type d -name lib -print0 2>/dev/null)
fi

if ((${#TORCH_LIB_PATHS[@]} > 0)); then
  JOINED="$(IFS=:; echo "${TORCH_LIB_PATHS[*]}")"
  export LD_LIBRARY_PATH="${JOINED}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
fi

# Optional: skip if user forces CPU
# export CUDA_VISIBLE_DEVICES=0
