#!/usr/bin/env bash
# 不启用 set -e，任何一步失败都继续跑完

PRESET="mingw-x64-release"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/${PRESET}"
EXE_PATH="${BUILD_DIR}/PassVault.exe"
TEST_EXE_PATH="${BUILD_DIR}/passvault_tests.exe"
CMAKE_CACHE="${BUILD_DIR}/CMakeCache.txt"
CACHED_WINDEPLOYQT=""
if [[ -f "${CMAKE_CACHE}" ]]; then
    CACHED_WINDEPLOYQT="$({
        sed -n 's/^PASSVAULT_WINDEPLOYQT:FILEPATH=//p' "${CMAKE_CACHE}"
        sed -n 's/^WINDEPLOYQT_EXECUTABLE:FILEPATH=//p' "${CMAKE_CACHE}"
    } | head -n 1)"
fi

if [[ -n "${WINDEPLOYQT:-}" ]]; then
    WINDEPLOYQT_CMD="${WINDEPLOYQT}"
elif [[ -n "${CACHED_WINDEPLOYQT}" && -x "${CACHED_WINDEPLOYQT}" ]]; then
    WINDEPLOYQT_CMD="${CACHED_WINDEPLOYQT}"
elif [[ -n "${QT_ROOT:-}" && -x "${QT_ROOT}/bin/windeployqt.exe" ]]; then
    WINDEPLOYQT_CMD="${QT_ROOT}/bin/windeployqt.exe"
elif command -v windeployqt.exe >/dev/null 2>&1; then
    WINDEPLOYQT_CMD="$(command -v windeployqt.exe)"
elif command -v windeployqt >/dev/null 2>&1; then
    WINDEPLOYQT_CMD="$(command -v windeployqt)"
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
if [[ ! -x "${EXE_PATH}" || ! -x "${TEST_EXE_PATH}" ]]; then
    echo "warn: expected Release executables were not produced" >&2
    exit 1
fi
if [[ -t 0 ]]; then
    read -r -p "press enter to exit... " _
fi
exit $((CONFIGURE_RC != 0 || BUILD_RC != 0 || ${DEPLOY_RC:-0} != 0))
