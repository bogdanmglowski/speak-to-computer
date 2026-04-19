#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

APP_ID="speak-to-computer"
APP_NAME="Speak to Computer"
BUILD_BINARY="${PROJECT_ROOT}/build-speak-to-computer/${APP_ID}"
ICON_SOURCE="${PROJECT_ROOT}/assets/${APP_ID}.svg"Ok, jestem zbierany, testowałem się na to.Ok, i testowanie. O, i testowanie tego.

INSTALL_BIN_DIR="${HOME}/.local/bin"
APPLICATIONS_DIR="${HOME}/.local/share/applications"
AUTOSTART_DIR="${HOME}/.config/autostart"
ICON_DIR="${HOME}/.local/share/icons/hicolor/scalable/apps"

INSTALLED_BINARY="${INSTALL_BIN_DIR}/${APP_ID}"
INSTALLED_ICON="${ICON_DIR}/${APP_ID}.svg"
DESKTOP_FILE="${APPLICATIONS_DIR}/${APP_ID}.desktop"
AUTOSTART_FILE="${AUTOSTART_DIR}/${APP_ID}.desktop"

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

echo "Building ${APP_ID}"
"${PROJECT_ROOT}/scripts/clean-build.sh"

if [[ ! -x "${BUILD_BINARY}" ]]; then
    echo "Build did not produce an executable: ${BUILD_BINARY}" >&2
    exit 1
fi

echo "Installing binary to ${INSTALLED_BINARY}"
install -d -m 0755 "${INSTALL_BIN_DIR}"
install -m 0755 "${BUILD_BINARY}" "${INSTALLED_BINARY}"

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
