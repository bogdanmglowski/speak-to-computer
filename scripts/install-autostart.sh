#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

APP_ID="speak-to-computer"
APP_NAME="Speak to Computer"
BUILD_BINARY="${PROJECT_ROOT}/build-speak-to-computer/${APP_ID}"
ICON_SOURCE="${PROJECT_ROOT}/assets/${APP_ID}.svg"
ACTIVATION_SOUND_SOURCE="${PROJECT_ROOT}/src/activation_sound.wav"
END_SOUND_SOURCE="${PROJECT_ROOT}/src/end_sound.wav"
WAKE_WORD_SIDECAR_SOURCE="${PROJECT_ROOT}/scripts/openwakeword_sidecar.py"
WAKE_WORD_RUNTIME_INSTALLER_SOURCE="${PROJECT_ROOT}/scripts/install-openwakeword-runtime.sh"
WAKE_WORD_REQUIREMENTS_SOURCE="${PROJECT_ROOT}/scripts/openwakeword/requirements.txt"
WAKE_WORD_SMOKE_TEST_SOURCE="${PROJECT_ROOT}/scripts/openwakeword/smoke_test.py"

INSTALL_BIN_DIR="${HOME}/.local/bin"
APP_DATA_DIR="${HOME}/.local/share/${APP_ID}"
WAKE_WORD_RUNTIME_DIR="${APP_DATA_DIR}/python"
APPLICATIONS_DIR="${HOME}/.local/share/applications"
AUTOSTART_DIR="${HOME}/.config/autostart"
ICON_DIR="${HOME}/.local/share/icons/hicolor/scalable/apps"

INSTALLED_BINARY="${INSTALL_BIN_DIR}/${APP_ID}"
INSTALLED_ACTIVATION_SOUND="${INSTALL_BIN_DIR}/activation_sound.wav"
INSTALLED_END_SOUND="${INSTALL_BIN_DIR}/end_sound.wav"
INSTALLED_WAKE_WORD_SIDECAR="${WAKE_WORD_RUNTIME_DIR}/openwakeword_sidecar.py"
INSTALLED_WAKE_WORD_RUNTIME_INSTALLER="${WAKE_WORD_RUNTIME_DIR}/install-openwakeword-runtime.sh"
INSTALLED_WAKE_WORD_REQUIREMENTS="${WAKE_WORD_RUNTIME_DIR}/openwakeword/requirements.txt"
INSTALLED_WAKE_WORD_SMOKE_TEST="${WAKE_WORD_RUNTIME_DIR}/openwakeword/smoke_test.py"
INSTALLED_ICON="${ICON_DIR}/${APP_ID}.svg"
DESKTOP_FILE="${APPLICATIONS_DIR}/${APP_ID}.desktop"
AUTOSTART_FILE="${AUTOSTART_DIR}/${APP_ID}.desktop"

has_libfvad_runtime() {
    if command -v ldconfig >/dev/null 2>&1; then
        if ldconfig -p 2>/dev/null | grep -qi 'libfvad\.so'; then
            return 0
        fi
    fi

    if command -v pkg-config >/dev/null 2>&1; then
        if pkg-config --exists fvad 2>/dev/null; then
            return 0
        fi
    fi

    return 1
}

run_with_privileges() {
    if [[ "$(id -u)" -eq 0 ]]; then
        "$@"
        return
    fi

    if command -v sudo >/dev/null 2>&1; then
        sudo "$@"
        return
    fi

    return 127
}

try_install_with_manager() {
    local manager="$1"
    shift
    local package
    for package in "$@"; do
        case "${manager}" in
            apt)
                if run_with_privileges apt-get install -y "${package}"; then
                    return 0
                fi
                ;;
            dnf)
                if run_with_privileges dnf install -y "${package}"; then
                    return 0
                fi
                ;;
            pacman)
                if run_with_privileges pacman -Sy --noconfirm "${package}"; then
                    return 0
                fi
                ;;
            zypper)
                if run_with_privileges zypper --non-interactive install "${package}"; then
                    return 0
                fi
                ;;
        esac
    done

    return 1
}

ensure_libfvad_runtime() {
    if has_libfvad_runtime; then
        echo "Detected libfvad runtime"
        return
    fi

    echo "libfvad runtime not detected. Trying to install it..."

    if command -v apt-get >/dev/null 2>&1; then
        if run_with_privileges apt-get update && try_install_with_manager apt libfvad-dev libfvad0; then
            :
        fi
    elif command -v dnf >/dev/null 2>&1; then
        if try_install_with_manager dnf fvad libfvad; then
            :
        fi
    elif command -v pacman >/dev/null 2>&1; then
        if try_install_with_manager pacman fvad; then
            :
        fi
    elif command -v zypper >/dev/null 2>&1; then
        if try_install_with_manager zypper fvad libfvad; then
            :
        fi
    else
        echo "Could not auto-install libfvad: unsupported package manager." >&2
    fi

    if has_libfvad_runtime; then
        echo "libfvad runtime ready"
    else
        echo "Warning: libfvad is still missing. VAD auto-stop will stay unavailable." >&2
    fi
}

write_desktop_file() {
    local target="$1"
    local comment="$2"
    local autostart="$3"
    local tmp

    tmp="$(mktemp "${target}.XXXXXX")"
    {
        printf '[Desktop Entry]\n'
        printf 'Type=Application\n'
        printf 'Name=%s\n' "${APP_NAME}"
        printf 'Comment=%s\n' "${comment}"
        printf 'Exec=%s\n' "${INSTALLED_BINARY}"
        printf 'Icon=%s\n' "${APP_ID}"
        printf 'Terminal=false\n'
        printf 'StartupNotify=false\n'
        if [[ "${autostart}" == "yes" ]]; then
            printf 'X-GNOME-Autostart-enabled=true\n'
            printf 'X-MATE-Autostart-enabled=true\n'
        else
            printf 'Categories=Utility;Accessibility;\n'
        fi
    } > "${tmp}"
    mv -f -- "${tmp}" "${target}"
    chmod 0644 "${target}"
}

if [[ ! -f "${ICON_SOURCE}" ]]; then
    echo "Missing icon asset: ${ICON_SOURCE}" >&2
    exit 1
fi
if [[ ! -f "${ACTIVATION_SOUND_SOURCE}" ]]; then
    echo "Missing activation sound asset: ${ACTIVATION_SOUND_SOURCE}" >&2
    exit 1
fi
if [[ ! -f "${END_SOUND_SOURCE}" ]]; then
    echo "Missing end sound asset: ${END_SOUND_SOURCE}" >&2
    exit 1
fi
if [[ ! -f "${WAKE_WORD_SIDECAR_SOURCE}" ]]; then
    echo "Missing wake-word sidecar script: ${WAKE_WORD_SIDECAR_SOURCE}" >&2
    exit 1
fi
if [[ ! -f "${WAKE_WORD_RUNTIME_INSTALLER_SOURCE}" ]]; then
    echo "Missing wake-word runtime installer: ${WAKE_WORD_RUNTIME_INSTALLER_SOURCE}" >&2
    exit 1
fi
if [[ ! -f "${WAKE_WORD_REQUIREMENTS_SOURCE}" ]]; then
    echo "Missing wake-word requirements file: ${WAKE_WORD_REQUIREMENTS_SOURCE}" >&2
    exit 1
fi
if [[ ! -f "${WAKE_WORD_SMOKE_TEST_SOURCE}" ]]; then
    echo "Missing wake-word smoke test: ${WAKE_WORD_SMOKE_TEST_SOURCE}" >&2
    exit 1
fi

ensure_libfvad_runtime

echo "Building ${APP_ID}"
"${PROJECT_ROOT}/scripts/clean-build.sh"

if [[ ! -x "${BUILD_BINARY}" ]]; then
    echo "Build did not produce an executable: ${BUILD_BINARY}" >&2
    exit 1
fi

echo "Installing binary to ${INSTALLED_BINARY}"
install -d -m 0755 "${INSTALL_BIN_DIR}"
install -m 0755 "${BUILD_BINARY}" "${INSTALLED_BINARY}"

echo "Installing activation sound to ${INSTALLED_ACTIVATION_SOUND}"
install -m 0644 "${ACTIVATION_SOUND_SOURCE}" "${INSTALLED_ACTIVATION_SOUND}"

echo "Installing end sound to ${INSTALLED_END_SOUND}"
install -m 0644 "${END_SOUND_SOURCE}" "${INSTALLED_END_SOUND}"

echo "Installing wake-word sidecar to ${INSTALLED_WAKE_WORD_SIDECAR}"
install -d -m 0755 "${WAKE_WORD_RUNTIME_DIR}/openwakeword"
install -m 0755 "${WAKE_WORD_SIDECAR_SOURCE}" "${INSTALLED_WAKE_WORD_SIDECAR}"
install -m 0755 "${WAKE_WORD_RUNTIME_INSTALLER_SOURCE}" "${INSTALLED_WAKE_WORD_RUNTIME_INSTALLER}"
install -m 0644 "${WAKE_WORD_REQUIREMENTS_SOURCE}" "${INSTALLED_WAKE_WORD_REQUIREMENTS}"
install -m 0644 "${WAKE_WORD_SMOKE_TEST_SOURCE}" "${INSTALLED_WAKE_WORD_SMOKE_TEST}"

echo "Installing icon to ${INSTALLED_ICON}"
install -d -m 0755 "${ICON_DIR}"
install -m 0644 "${ICON_SOURCE}" "${INSTALLED_ICON}"

echo "Writing desktop entry ${DESKTOP_FILE}"
install -d -m 0755 "${APPLICATIONS_DIR}"
write_desktop_file "${DESKTOP_FILE}" "Speech-to-text dictation" "no"

echo "Writing autostart entry ${AUTOSTART_FILE}"
install -d -m 0755 "${AUTOSTART_DIR}"
write_desktop_file "${AUTOSTART_FILE}" "Start Speak to Computer on login" "yes"

echo "Installed ${APP_NAME}."
echo "Run it now with: ${INSTALLED_BINARY}"
echo
echo "To install wake-word Python runtime run:"
echo "  ${INSTALLED_WAKE_WORD_RUNTIME_INSTALLER}"
