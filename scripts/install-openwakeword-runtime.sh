#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="${SCRIPT_DIR}/.venv"
REQUIREMENTS_FILE="${SCRIPT_DIR}/openwakeword/requirements.txt"
SMOKE_TEST_FILE="${SCRIPT_DIR}/openwakeword/smoke_test.py"

if [[ ! -f "${REQUIREMENTS_FILE}" ]]; then
    echo "Missing requirements file: ${REQUIREMENTS_FILE}" >&2
    exit 1
fi
if [[ ! -f "${SMOKE_TEST_FILE}" ]]; then
    echo "Missing smoke test file: ${SMOKE_TEST_FILE}" >&2
    exit 1
fi

supports_openwakeword_runtime() {
    local candidate="$1"
    "${candidate}" - <<'PY'
import sys
ok = (sys.version_info.major == 3 and 10 <= sys.version_info.minor < 12)
raise SystemExit(0 if ok else 1)
PY
}

select_python_bin() {
    if [[ -n "${PYTHON_BIN:-}" ]]; then
        if ! command -v "${PYTHON_BIN}" >/dev/null 2>&1; then
            echo "Missing PYTHON_BIN=${PYTHON_BIN} in PATH" >&2
            return 1
        fi
        if ! supports_openwakeword_runtime "${PYTHON_BIN}"; then
            local version
            version="$("${PYTHON_BIN}" --version 2>&1 || echo "unknown")"
            echo "Unsupported ${version}. openwakeword runtime requires Python 3.10 or 3.11." >&2
            return 1
        fi
        echo "${PYTHON_BIN}"
        return 0
    fi

    local candidates=("python3.11" "python3.10" "python3")
    local candidate
    for candidate in "${candidates[@]}"; do
        if ! command -v "${candidate}" >/dev/null 2>&1; then
            continue
        fi
        if supports_openwakeword_runtime "${candidate}"; then
            echo "${candidate}"
            return 0
        fi
    done

    if command -v uv >/dev/null 2>&1; then
        echo "No compatible Python in PATH, trying uv-managed Python 3.11..." >&2
        if uv python install 3.11 >/dev/null 2>&1; then
            local uv_python_bin
            uv_python_bin="$(uv python find 3.11 2>/dev/null || true)"
            if [[ -n "${uv_python_bin}" ]] && supports_openwakeword_runtime "${uv_python_bin}"; then
                echo "${uv_python_bin}"
                return 0
            fi
        fi
    fi

    return 1
}

PYTHON_BIN="$(select_python_bin || true)"
if [[ -z "${PYTHON_BIN}" ]]; then
    echo "Could not find a compatible Python interpreter (3.10 or 3.11)." >&2
    if command -v python3 >/dev/null 2>&1; then
        echo "Detected: $(python3 --version 2>&1)" >&2
    fi
    echo "Install python3.11 (or python3.10), or install uv-managed Python 3.11:" >&2
    echo "  uv python install 3.11" >&2
    echo "Then re-run with:" >&2
    echo "  PYTHON_BIN=python3.11 ${SCRIPT_DIR}/install-openwakeword-runtime.sh" >&2
    exit 1
fi

echo "Using Python interpreter: ${PYTHON_BIN} ($("${PYTHON_BIN}" --version 2>&1))"
echo "Recreating wake-word Python venv at ${VENV_DIR}"
rm -rf "${VENV_DIR}"
"${PYTHON_BIN}" -m venv "${VENV_DIR}"

echo "Installing Python dependencies"
"${VENV_DIR}/bin/python" -m pip install --upgrade pip wheel setuptools
"${VENV_DIR}/bin/python" -m pip install -r "${REQUIREMENTS_FILE}"

echo "Running wake-word smoke test"
"${VENV_DIR}/bin/python" "${SMOKE_TEST_FILE}"

echo "Wake-word runtime ready"
echo "Interpreter: ${VENV_DIR}/bin/python"
