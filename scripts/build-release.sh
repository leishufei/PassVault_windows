#!/usr/bin/env bash
# 不启用 set -e，任何一步失败都继续跑完

PRESET="mingw-x64-release"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/${PRESET}"
EXE_DIR="${BUILD_DIR}/src"
EXE_PATH="${EXE_DIR}/passvault.exe"

if [[ -n "${WINDEPLOYQT:-}" ]]; then
    WINDEPLOYQT_CMD="${WINDEPLOYQT}"
elif command -v windeployqt.exe >/dev/null 2>&1; then
    WINDEPLOYQT_CMD="$(command -v windeployqt.exe)"
elif command -v windeployqt >/dev/null 2>&1; then
    WINDEPLOYQT_CMD="$(command -v windeployqt)"
elif [[ -n "${QT_ROOT:-}" && -x "${QT_ROOT}/bin/windeployqt.exe" ]]; then
    WINDEPLOYQT_CMD="${QT_ROOT}/bin/windeployqt.exe"
else
    WINDEPLOYQT_CMD=""
fi

if [[ -z "${VCPKG_ROOT:-}" ]]; then
    echo "warn: VCPKG_ROOT is not set" >&2
fi

if [[ "${1:-}" == "--clean" ]]; then
    echo ">>> cleaning ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
fi

cd "${ROOT_DIR}" || echo "warn: cd ${ROOT_DIR} failed" >&2

echo ">>> configure"
cmake --preset "${PRESET}"
CONFIGURE_RC=$?
[[ ${CONFIGURE_RC} -ne 0 ]] && echo "warn: configure exited with ${CONFIGURE_RC}" >&2

echo ">>> build"
cmake --build --preset "${PRESET}" -j
BUILD_RC=$?
[[ ${BUILD_RC} -ne 0 ]] && echo "warn: build exited with ${BUILD_RC}" >&2

if [[ ! -x "${EXE_PATH}" ]]; then
    echo "warn: ${EXE_PATH} not found, skipping windeployqt" >&2
else
    echo ">>> windeployqt"
    if [[ -z "${WINDEPLOYQT_CMD}" || ! -x "${WINDEPLOYQT_CMD}" ]]; then
        echo "warn: windeployqt not found; add it to PATH or set QT_ROOT/WINDEPLOYQT, skipping" >&2
    else
        "${WINDEPLOYQT_CMD}" \
            --release \
            --no-translations \
            --no-system-d3d-compiler \
            "${EXE_PATH}"
        DEPLOY_RC=$?
        [[ ${DEPLOY_RC} -ne 0 ]] && echo "warn: windeployqt exited with ${DEPLOY_RC}" >&2
    fi
fi

echo ">>> done: ${EXE_PATH}"
read -r -p "press enter to exit... " _
# exit 0
