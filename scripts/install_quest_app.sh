#!/usr/bin/env bash

set -euo pipefail

# Build, install, and optionally launch the existing ILLIXR Android client used
# by the native Boba streaming profile. USB/adb is needed for installation only;
# normal frame, pose, and controller traffic travels over Wi-Fi.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

BUILD_VARIANT="debug"
DEVICE_SERIAL=""
SDK_ROOT_OVERRIDE=""
JAVA_HOME_OVERRIDE=""
ADB_OVERRIDE=""
BUILD_APP=1
LAUNCH_APP=1

# ---- Command-line interface -------------------------------------------------

usage() {
    cat <<'EOF'
Build and install the native ILLIXR Quest application.

Usage:
  ./scripts/install_quest_app.sh [options]

Options:
  --serial SERIAL       Select a device when more than one is connected.
  --release             Build and install the release APK instead of debug.
  --no-build            Install the APK already present under app/build/outputs.
  --no-launch           Do not launch ILLIXRApp after installation.
  --android-sdk PATH    Android SDK root (otherwise auto-detected).
  --java-home PATH      JDK 17+ root (otherwise auto-detected).
  --adb PATH            adb executable (otherwise auto-detected).
  -h, --help            Show this help.

The initial installation requires a developer-enabled Quest authorized through
USB debugging. Runtime streaming does not use USB.
EOF
}

fail() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

while (($# > 0)); do
    case "$1" in
        --serial)
            (($# >= 2)) || fail "--serial requires a value"
            DEVICE_SERIAL="$2"
            shift 2
            ;;
        --release)
            BUILD_VARIANT="release"
            shift
            ;;
        --no-build)
            BUILD_APP=0
            shift
            ;;
        --no-launch)
            LAUNCH_APP=0
            shift
            ;;
        --android-sdk)
            (($# >= 2)) || fail "--android-sdk requires a value"
            SDK_ROOT_OVERRIDE="$2"
            shift 2
            ;;
        --java-home)
            (($# >= 2)) || fail "--java-home requires a value"
            JAVA_HOME_OVERRIDE="$2"
            shift 2
            ;;
        --adb)
            (($# >= 2)) || fail "--adb requires a value"
            ADB_OVERRIDE="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            fail "unknown option: $1"
            ;;
    esac
done

# ---- Toolchain discovery ----------------------------------------------------

# Normalize Java's legacy and modern version formats to a numeric major version.
java_major_version() {
    local java_bin="$1"
    local version
    version="$("${java_bin}" -version 2>&1 | sed -n '1{s/.*version "\([^"]*\)".*/\1/p;}')"
    if [[ "${version}" == 1.* ]]; then
        version="${version#1.}"
    fi
    printf '%s\n' "${version%%[._-]*}"
}

# Prefer explicit overrides, then active shell tools, then common local Android
# development locations. Gradle 8 requires a JDK with major version 17 or newer.
select_java_home() {
    local -a candidates=()
    local candidate java_path major studio_jbr

    [[ -n "${JAVA_HOME_OVERRIDE}" ]] && candidates+=("${JAVA_HOME_OVERRIDE}")
    [[ -n "${JAVA_HOME:-}" ]] && candidates+=("${JAVA_HOME}")
    if command -v java >/dev/null 2>&1; then
        java_path="$(readlink -f "$(command -v java)")"
        candidates+=("$(dirname -- "$(dirname -- "${java_path}")")")
    fi
    candidates+=(
        "${REPO_ROOT}/../.tools/jdk17"
        "/opt/android-studio/jbr"
        "/usr/lib/jvm/java-21-openjdk-amd64"
        "/usr/lib/jvm/java-17-openjdk-amd64"
    )
    for studio_jbr in "${HOME}"/.local/share/JetBrains/Toolbox/apps/AndroidStudio/ch-0/*/jbr; do
        candidates+=("${studio_jbr}")
    done

    for candidate in "${candidates[@]}"; do
        [[ -x "${candidate}/bin/java" ]] || continue
        major="$(java_major_version "${candidate}/bin/java")"
        if [[ "${major}" =~ ^[0-9]+$ ]] && ((major >= 17)); then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done
    return 1
}

# Locate an SDK by its adb executable because adb is required by every install
# path and gives us the SDK root without depending on Android Studio settings.
select_android_sdk() {
    local -a candidates=()
    local adb_path candidate

    [[ -n "${SDK_ROOT_OVERRIDE}" ]] && candidates+=("${SDK_ROOT_OVERRIDE}")
    [[ -n "${ANDROID_SDK_ROOT:-}" ]] && candidates+=("${ANDROID_SDK_ROOT}")
    [[ -n "${ANDROID_HOME:-}" ]] && candidates+=("${ANDROID_HOME}")
    if [[ -n "${ADB_OVERRIDE}" ]]; then
        candidates+=("$(cd -- "$(dirname -- "${ADB_OVERRIDE}")/.." && pwd)")
    elif command -v adb >/dev/null 2>&1; then
        adb_path="$(readlink -f "$(command -v adb)")"
        candidates+=("$(dirname -- "$(dirname -- "${adb_path}")")")
    fi
    candidates+=(
        "${REPO_ROOT}/../.tools/android-sdk"
        "${REPO_ROOT}/../.tools/android"
        "${HOME}/Android/Sdk"
        "${HOME}/Library/Android/sdk"
    )

    for candidate in "${candidates[@]}"; do
        if [[ -x "${candidate}/platform-tools/adb" ]]; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done
    return 1
}

JAVA_HOME_SELECTED="$(select_java_home)" || fail "JDK 17 or newer was not found; pass --java-home PATH"
ANDROID_SDK_SELECTED="$(select_android_sdk)" || fail "Android SDK was not found; pass --android-sdk PATH"
ADB_BIN="${ADB_OVERRIDE:-${ANDROID_SDK_SELECTED}/platform-tools/adb}"

# `protoc` is provided by the ILLIXR Conda environment and is consumed by the
# native Android build, so report the missing environment before invoking Gradle.
[[ -x "${ADB_BIN}" ]] || fail "adb is not executable: ${ADB_BIN}"
command -v protoc >/dev/null 2>&1 || fail \
    "protoc was not found; activate the ILLIXR Conda environment before running this script"

declare -a missing_packages=()
[[ -d "${ANDROID_SDK_SELECTED}/platforms/android-33" ]] || missing_packages+=("platforms;android-33")
[[ -d "${ANDROID_SDK_SELECTED}/ndk/27.2.12479018" ]] || missing_packages+=("ndk;27.2.12479018")
[[ -d "${ANDROID_SDK_SELECTED}/cmake/3.22.1" ]] || missing_packages+=("cmake;3.22.1")
if ((${#missing_packages[@]} > 0)); then
    printf 'The Android SDK is missing required packages:\n' >&2
    printf '  %s\n' "${missing_packages[@]}" >&2
    printf 'Install them with sdkmanager after reviewing the Android SDK licenses.\n' >&2
    exit 1
fi

# Export the discovered tools only for this process and its Gradle children.
export JAVA_HOME="${JAVA_HOME_SELECTED}"
export ANDROID_SDK_ROOT="${ANDROID_SDK_SELECTED}"
export ANDROID_HOME="${ANDROID_SDK_SELECTED}"
export PATH="${JAVA_HOME}/bin:${ANDROID_SDK_ROOT}/platform-tools:${PATH}"

# ---- Android dependency preparation ----------------------------------------

# The Android dependency bundle is a submodule. Expand its packaged headers and
# arm64 libraries only when the build-ready files are absent.
if [[ ! -f "${REPO_ROOT}/android_libraries/third_party/includes.tar.gz" ]]; then
    command -v git >/dev/null 2>&1 || fail "git is required to initialize android_libraries"
    printf 'Initializing the Android dependency submodule...\n'
    git -C "${REPO_ROOT}" submodule update --init --recursive android_libraries
fi

if [[ ! -f "${REPO_ROOT}/android_libraries/third_party/include/boost/version.hpp" ||
      ! -f "${REPO_ROOT}/android_libraries/third_party/lib/arm64-v8a/libspdlog_android.so" ||
      ! -f "${REPO_ROOT}/android_libraries/third_party/sdk/native/libs/arm64-v8a/libopencv_java4.so" ]]; then
    command -v tar >/dev/null 2>&1 || fail "tar is required to prepare Android dependencies"
    command -v unzip >/dev/null 2>&1 || fail "unzip is required to prepare Android dependencies"
    printf 'Preparing the packaged Android dependencies...\n'
    (cd "${REPO_ROOT}/android_libraries/third_party" && ./prepare.sh)
fi

# ---- APK build and device installation -------------------------------------

# Keep variant-to-task and variant-to-output selection together so --no-build
# installs the same artifact that a normal invocation would have produced.
GRADLE_TASK="assembleDebug"
APK_PATH="${REPO_ROOT}/app/build/outputs/apk/debug/app-debug.apk"
if [[ "${BUILD_VARIANT}" == "release" ]]; then
    [[ -f "${HOME}/illixr.keystore" ]] || fail "release installation requires ${HOME}/illixr.keystore"
    GRADLE_TASK="assembleRelease"
    APK_PATH="${REPO_ROOT}/app/build/outputs/apk/release/app-release.apk"
fi

if ((BUILD_APP)); then
    printf 'Building ILLIXRApp (%s)...\n' "${BUILD_VARIANT}"
    (cd "${REPO_ROOT}" && ./gradlew --no-daemon ":app:${GRADLE_TASK}")
fi
[[ -f "${APK_PATH}" ]] || fail "APK not found: ${APK_PATH}"

# adb can address only one target implicitly. Require --serial when multiple
# devices are present and reject unauthorized/offline devices up front.
mapfile -t CONNECTED_DEVICES < <("${ADB_BIN}" devices | awk 'NR > 1 && $2 == "device" {print $1}')
if [[ -n "${DEVICE_SERIAL}" ]]; then
    DEVICE_FOUND=0
    for CONNECTED_DEVICE in "${CONNECTED_DEVICES[@]}"; do
        if [[ "${CONNECTED_DEVICE}" == "${DEVICE_SERIAL}" ]]; then
            DEVICE_FOUND=1
            break
        fi
    done
    if ((DEVICE_FOUND == 0)); then
        fail "device ${DEVICE_SERIAL} is not connected and authorized"
    fi
elif ((${#CONNECTED_DEVICES[@]} == 1)); then
    DEVICE_SERIAL="${CONNECTED_DEVICES[0]}"
elif ((${#CONNECTED_DEVICES[@]} == 0)); then
    fail "no authorized Quest found; connect USB, enable developer mode, and approve the debugging prompt"
else
    printf 'Multiple Android devices are connected:\n' >&2
    printf '  %s\n' "${CONNECTED_DEVICES[@]}" >&2
    fail "select the Quest with --serial SERIAL"
fi

# Reinstall in place to preserve app data. A signing-key mismatch needs an
# explicit uninstall because removing an installed package is destructive.
ADB_DEVICE=("${ADB_BIN}" -s "${DEVICE_SERIAL}")
DEVICE_MODEL="$("${ADB_DEVICE[@]}" shell getprop ro.product.model 2>/dev/null | tr -d '\r')"
printf 'Installing on %s (%s)...\n' "${DEVICE_MODEL:-Android device}" "${DEVICE_SERIAL}"
if ! INSTALL_OUTPUT="$("${ADB_DEVICE[@]}" install -r "${APK_PATH}" 2>&1)"; then
    printf '%s\n' "${INSTALL_OUTPUT}" >&2
    if [[ "${INSTALL_OUTPUT}" == *INSTALL_FAILED_UPDATE_INCOMPATIBLE* ]]; then
        printf '%s\n' \
            "The installed app was signed with a different key. Remove it manually with:" \
            "  ${ADB_BIN} -s ${DEVICE_SERIAL} uninstall com.example.native_activity" >&2
    fi
    exit 1
fi
printf '%s\n' "${INSTALL_OUTPUT}"

if ((LAUNCH_APP)); then
    printf 'Launching ILLIXRApp...\n'
    "${ADB_DEVICE[@]}" shell am start \
        -n com.example.native_activity/com.example.ILLIXR.ILLIXRNativeActivity >/dev/null
fi

# Report the Wi-Fi address needed by desktop `--quest-ip`. The device may be
# unplugged after this point; adb is not part of the runtime transport.
QUEST_IP="$("${ADB_DEVICE[@]}" shell ip route 2>/dev/null | tr -d '\r' | awk '/wlan[0-9]/ && / src / {for (i = 1; i <= NF; ++i) if ($i == "src") {print $(i + 1); exit}}')"
APP_STATUS=""
if ((LAUNCH_APP)); then
    APP_STATUS=" and running"
fi
printf '\nILLIXRApp is installed%s.\n' "${APP_STATUS}"
if [[ -n "${QUEST_IP}" ]]; then
    printf 'Detected Quest Wi-Fi address: %s\n' "${QUEST_IP}"
    printf 'Start desktop ILLIXR with: --quest-ip %s\n' "${QUEST_IP}"
else
    printf 'Open ILLIXRApp on the Quest, determine its Wi-Fi address, and pass it with --quest-ip.\n'
fi
