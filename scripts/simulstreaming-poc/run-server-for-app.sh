#!/usr/bin/env bash
# Started by speak-to-computer (RealtimeSidecar). Sets CUDA lib path, logs to a file,
# runs SimulStreaming TCP server in the foreground (one process, easy to kill).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# GUI/NoMachine/Qt sessions inject LD_PRELOAD (libnxegl), system CUDA, and a
# huge desktop environment. Those can hang torch CUDA init when this is launched
# as a detached child of the tray app. Sanitize hard before the venv.
unset LD_PRELOAD || true
unset PYTHONPATH || true
unset PYTHONHOME || true
unset DISPLAY || true
unset WAYLAND_DISPLAY || true
unset QT_PLUGIN_PATH || true
unset QT_QPA_PLATFORM_PLUGIN_PATH || true
unset SESSION_MANAGER || true
unset XDG_SESSION_TYPE || true
unset __GLX_VENDOR_LIBRARY_NAME || true
unset __NV_PRIME_RENDER_OFFLOAD || true
unset __VK_LAYER_NV_optimus || true

# shellcheck disable=SC1091
source "${ROOT}/env.sh"

# Prefer ONLY venv torch/nvidia libs — drop system CUDA that env.sh may have appended.
SITE="${ROOT}/.venv/lib/python3.12/site-packages"
if [[ ! -d "${SITE}" ]]; then
  SITE="$(echo "${ROOT}"/.venv/lib/python*/site-packages | awk '{print $1}')"
fi
PURE_LD=()
if [[ -d "${SITE}/torch/lib" ]]; then
  PURE_LD+=("${SITE}/torch/lib")
fi
if [[ -d "${SITE}/nvidia" ]]; then
  while IFS= read -r -d '' libdir; do
    PURE_LD+=("${libdir}")
  done < <(find "${SITE}/nvidia" -type d -name lib -print0 2>/dev/null)
fi
if ((${#PURE_LD[@]} > 0)); then
  export LD_LIBRARY_PATH="$(IFS=:; echo "${PURE_LD[*]}")"
fi

HOST="${1:-127.0.0.1}"
PORT="${2:-43001}"
MODEL="${3:-medium}"
LANGUAGE="${4:-pl}"
LOG_FILE="${5:-${XDG_CACHE_HOME:-$HOME/.cache}/speak-to-computer/realtime-server.log}"
# Optional dictionary file (one term per line, # comments) → --static_init_prompt
DICTIONARY_FILE="${6:-}"

# Whisper/SimulStreaming accept "auto"; normalize empty/locale tags.
case "${LANGUAGE}" in
  ""|auto|detect) LANGUAGE=auto ;;
  en_*|en:*|en-*|en) LANGUAGE=en ;;
  pl_*|pl:*|pl-*|pl) LANGUAGE=pl ;;
esac

# Soft terminology bias for SimulStreaming (not a forced lexicon).
STATIC_PROMPT=""
if [[ -n "${DICTIONARY_FILE}" && -f "${DICTIONARY_FILE}" ]]; then
  STATIC_PROMPT="$(
    awk '
      /^[[:space:]]*#/ { next }
      /^[[:space:]]*$/ { next }
      {
        gsub(/^[[:space:]]+|[[:space:]]+$/, "", $0)
        if (length($0) == 0) next
        if (n++) printf ", "
        printf "%s", $0
      }
    ' "${DICTIONARY_FILE}"
  )"
  # Keep last ~900 chars if the list is huge (prompt budget is small).
  if ((${#STATIC_PROMPT} > 900)); then
    STATIC_PROMPT="${STATIC_PROMPT: -900}"
    STATIC_PROMPT="${STATIC_PROMPT#*, }"
  fi
fi

mkdir -p "$(dirname "${LOG_FILE}")"
: >"${LOG_FILE}"

cd "${ROOT}/SimulStreaming"

{
  echo "==> $(date -Iseconds) run-server-for-app.sh pid=$$"
  echo "    host=${HOST} port=${PORT} model=${MODEL} language=${LANGUAGE}"
  echo "    dictionary=${DICTIONARY_FILE:-<none>} prompt_chars=${#STATIC_PROMPT}"
  echo "    python=$(command -v python)"
  echo "    LD_PRELOAD=${LD_PRELOAD-<unset>}"
  echo "    LD_LIBRARY_PATH(prefix)=${LD_LIBRARY_PATH:0:240}"
  echo "    starting python…"
} >>"${LOG_FILE}"

export PYTHONUNBUFFERED=1
# Avoid CUDA/OpenGL interop surprises under desktop sessions.
export CUDA_MODULE_LOADING="${CUDA_MODULE_LOADING:-LAZY}"
export PYTORCH_NVML_BASED_CUDA_CHECK="${PYTORCH_NVML_BASED_CUDA_CHECK:-1}"

PY_BIN="$(command -v python)"

# Close inherited fds from the Qt/NoMachine parent. Open DRM/NVIDIA handles
# from the GUI process can deadlock CUDA init in the child (sibling shell
# launch works; child-of-tray-app hung with zero python log output).
if [[ -d /proc/$$/fd ]]; then
  for fd in /proc/$$/fd/*; do
    fdnum="${fd##*/}"
    if [[ "${fdnum}" =~ ^[0-9]+$ ]] && ((fdnum > 2)); then
      eval "exec ${fdnum}>&-" 2>/dev/null || true
    fi
  done
fi

echo "exec python $(date -Iseconds) py=${PY_BIN}" >>"${LOG_FILE}"

SERVER_ARGS=(
  -u simulstreaming_whisper_server.py
  --host "${HOST}"
  --port "${PORT}"
  --language "${LANGUAGE}"
  --task transcribe
  --model_path "${MODEL}"
  --min-chunk-size 0.6
  --frame_threshold 30
  --audio_min_len 0.0
  --vac
  --out-txt
  -l INFO
  --never_fire
)
if [[ -n "${STATIC_PROMPT}" ]]; then
  SERVER_ARGS+=(--static_init_prompt "${STATIC_PROMPT}")
fi

# Clean env for the server process: keep only what CUDA/python need.
exec env -i \
  HOME="${HOME}" \
  USER="${USER:-}" \
  LOGNAME="${LOGNAME:-}" \
  PATH="${PATH}" \
  LD_LIBRARY_PATH="${LD_LIBRARY_PATH}" \
  PYTHONUNBUFFERED=1 \
  CUDA_MODULE_LOADING="${CUDA_MODULE_LOADING}" \
  PYTORCH_NVML_BASED_CUDA_CHECK="${PYTORCH_NVML_BASED_CUDA_CHECK}" \
  LANG="${LANG:-C.UTF-8}" \
  LC_ALL="${LC_ALL:-C.UTF-8}" \
  XDG_CACHE_HOME="${XDG_CACHE_HOME:-${HOME}/.cache}" \
  XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-${HOME}/.config}" \
  TMPDIR="${TMPDIR:-/tmp}" \
  "${PY_BIN}" "${SERVER_ARGS[@]}" \
  >>"${LOG_FILE}" 2>&1
