#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
RUNTIME_DIR="${XDG_DATA_HOME:-${HOME}/.local/share}/speak-to-computer/parakeet"
PYTHON="${RUNTIME_DIR}/.venv/bin/python"

mkdir -p "${RUNTIME_DIR}"
if command -v uv >/dev/null 2>&1; then
    if [[ ! -x "${PYTHON}" ]]; then
        uv venv --python "${PYTHON_BIN:-python3}" "${RUNTIME_DIR}/.venv"
    fi
    uv pip install --python "${PYTHON}" -r "${SCRIPT_DIR}/parakeet/requirements.txt"
else
    if [[ ! -x "${PYTHON}" ]]; then
        "${PYTHON_BIN:-python3}" -m venv "${RUNTIME_DIR}/.venv"
    fi
    "${PYTHON}" -m pip install -r "${SCRIPT_DIR}/parakeet/requirements.txt"
fi

if [[ "${SCRIPT_DIR}" != "${RUNTIME_DIR}" ]]; then
    install -m 0644 "${SCRIPT_DIR}/parakeet_transcribe.py" "${RUNTIME_DIR}/parakeet_transcribe.py"
fi

# The first run downloads the model (about 2.5 GB) into RUNTIME_DIR/models and runs one silent request on the GPU.
(cd "${RUNTIME_DIR}" && "${PYTHON}" -c 'import parakeet_transcribe as sidecar; sidecar.warm_up(sidecar.load_model()); print("Parakeet runtime ready")')
echo "Interpreter: ${PYTHON}"
