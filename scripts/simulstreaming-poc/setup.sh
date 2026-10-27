#!/usr/bin/env bash
# One-time setup for SimulStreaming CLI POC (GPU Whisper streaming).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="${ROOT}/SimulStreaming"
VENV_DIR="${ROOT}/.venv"
PORT="${SIMULSTREAMING_PORT:-43001}"

echo "==> SimulStreaming POC setup"
echo "    dir:  ${ROOT}"
echo "    port: ${PORT} (server default later)"

if ! command -v git >/dev/null; then
  echo "ERROR: git not found" >&2
  exit 1
fi
if ! command -v uv >/dev/null && ! command -v python3 >/dev/null; then
  echo "ERROR: need uv or python3" >&2
  exit 1
fi

if [[ ! -d "${REPO_DIR}/.git" ]]; then
  echo "==> Cloning ufal/SimulStreaming"
  git clone --depth 1 https://github.com/ufal/SimulStreaming.git "${REPO_DIR}"
else
  echo "==> SimulStreaming already cloned (pull optional: git -C ${REPO_DIR} pull)"
fi

if command -v uv >/dev/null; then
  echo "==> Creating venv with uv (Python 3.12)"
  uv venv --python 3.12 "${VENV_DIR}"
  # shellcheck disable=SC1091
  source "${VENV_DIR}/bin/activate"
  echo "==> Installing PyTorch (CUDA 12.8 wheel index — works with recent NVIDIA drivers)"
  # RTX 50xx needs a recent torch+cuda build; 12.8 is a safe default in 2026.
  uv pip install --python "${VENV_DIR}/bin/python" \
    torch torchaudio \
    --index-url https://download.pytorch.org/whl/cu128
  echo "==> Installing SimulStreaming whisper requirements"
  uv pip install --python "${VENV_DIR}/bin/python" -r "${REPO_DIR}/requirements_whisper.txt"
else
  echo "==> Creating venv with python3 -m venv"
  python3 -m venv "${VENV_DIR}"
  # shellcheck disable=SC1091
  source "${VENV_DIR}/bin/activate"
  pip install -U pip
  pip install torch torchaudio --index-url https://download.pytorch.org/whl/cu128
  pip install -r "${REPO_DIR}/requirements_whisper.txt"
fi

echo "==> Checking CUDA (with venv libs preferred over system CUDA in LD_LIBRARY_PATH)"
# shellcheck disable=SC1091
source "${ROOT}/env.sh"
python - <<'PY'
import torch
print(f"torch={torch.__version__}")
print(f"cuda_available={torch.cuda.is_available()}")
if torch.cuda.is_available():
    print(f"gpu={torch.cuda.get_device_name(0)}")
    print(f"vram_gb={torch.cuda.get_device_properties(0).total_memory / 1024**3:.1f}")
    print(f"cudnn={torch.backends.cudnn.version()}")
    x = torch.randn(1, 3, 16, 16, device="cuda")
    y = torch.nn.Conv2d(3, 4, 3).cuda()(x)
    torch.cuda.synchronize()
    print(f"conv_ok={tuple(y.shape)}")
else:
    print("WARNING: CUDA not available — streaming will be slow on CPU")
PY

# Small sample wav for file mode (English). Optional.
SAMPLE="${ROOT}/sample-jfk.wav"
if [[ ! -f "${SAMPLE}" ]]; then
  echo "==> Downloading short sample WAV for file-mode test"
  if curl -fsSL -o "${SAMPLE}" \
    "https://github.com/ggerganov/whisper.cpp/raw/master/samples/jfk.wav"; then
    echo "    saved ${SAMPLE}"
  else
    echo "    (sample download failed — skip; mic mode still works)"
    rm -f "${SAMPLE}" || true
  fi
fi

cat <<EOF

============================================================
Setup done.

Next (file test, no mic — ~1–3 min first run, downloads model):
  ${ROOT}/run-file.sh

Next (live mic → partial text in terminal):
  Terminal A:  ${ROOT}/run-server.sh
  Terminal B:  ${ROOT}/run-mic-client.sh

Defaults: ASR_LANG=pl (server) / en (file), MODEL=small (fits ~8GB VRAM).
Do not use env LANGUAGE=... (system locale, e.g. en_US:en, breaks Whisper).

Override:
  ASR_LANG=en MODEL=medium ${ROOT}/run-server.sh
============================================================
EOF
