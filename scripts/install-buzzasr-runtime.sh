#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
RUNTIME_DIR="${XDG_DATA_HOME:-${HOME}/.local/share}/speak-to-computer/buzzasr"
PYTHON="${RUNTIME_DIR}/.venv/bin/python"

mkdir -p "${RUNTIME_DIR}"
if command -v uv >/dev/null 2>&1; then
    if [[ ! -x "${PYTHON}" ]]; then
        uv venv --python "${PYTHON_BIN:-python3}" "${RUNTIME_DIR}/.venv"
    fi
    uv pip install --python "${PYTHON}" -r "${SCRIPT_DIR}/buzzasr/requirements.txt"
else
    if [[ ! -x "${PYTHON}" ]]; then
        "${PYTHON_BIN:-python3}" -m venv "${RUNTIME_DIR}/.venv"
    fi
    "${PYTHON}" -m pip install -r "${SCRIPT_DIR}/buzzasr/requirements.txt"
fi

if [[ "${SCRIPT_DIR}" != "${RUNTIME_DIR}" ]]; then
    install -m 0644 "${SCRIPT_DIR}/buzzasr_transcribe.py" "${RUNTIME_DIR}/buzzasr_transcribe.py"
fi
"${PYTHON}" -c 'import torch, transformers, soundfile; assert torch.cuda.is_available(), "CUDA is unavailable"; print("BuzzASR runtime ready:", torch.cuda.get_device_name(0))'
echo "Interpreter: ${PYTHON}"
