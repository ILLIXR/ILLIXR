#!/usr/bin/env bash
set -Eeuo pipefail

readonly BOBA_PUBLIC_REPOSITORY="https://github.com/jianxiapyh/Boba-Public.git"
readonly BOBA_PUBLIC_REF="7f9ef3bdc648751f40e50bf8df6160bbd0b73e5c"
readonly BOBA_DEMO_REPOSITORY="https://github.com/jianxiapyh/Boba-Demo.git"
readonly BOBA_DEMO_REF="077a95bb591152f63b0d392f00ef8001e2d7106f"
readonly BOBA_ENVIRONMENT="boba-cu132"
readonly GDOWN_VERSION="6.1.0"

readonly DEMO_LFS_PATH="assets/sloth/sloth.ply"
readonly DEMO_LFS_SHA256="fc0301db3e5fd077d153e3bb2d68cf609db1ebc6968932101f34d731b6aec5d2"
readonly DEMO_LFS_URL="https://media.githubusercontent.com/media/jianxiapyh/Boba-Demo/${BOBA_DEMO_REF}/${DEMO_LFS_PATH}"
readonly DEMO_MANAGED_PATHS="${DEMO_LFS_PATH}:boba_app.sh:boba_quest_immersive.py:gaussian_splatting/_gsplat_vendor.py:tools/fetch_demo_case_assets.py:qqtt/garden_assets.py"

readonly PATCHED_BOBA_APP_SHA256="d9193820fb1c79ee87d22389dbef9624d5631d5a4ddc93c116f74a0004a6b9b2"
readonly PATCHED_BOBA_MAIN_SHA256="bb486ed6643813a2346e5e624dd9a07fa50b76a5d8785ebf8bff6f891a9619d5"
readonly PATCHED_GSPLAT_VENDOR_SHA256="c4e1b07377bb0993c321cdb6cb5ce0bf2b1b0ecdafb941bd0172500ef8fdc3e0"
readonly PATCHED_DEMO_ASSETS_SHA256="0f42bf6eef01f341f9f9dc6a67f69eef24eefb6879da355b74560b8d8ed6fdde"
readonly PATCHED_GARDEN_ASSETS_SHA256="3431640e18d85f8ccd38b8501fc9e7ed3ed185b75bea76ffad2f4ac7d7b6803d"

readonly -a PUBLIC_ASSET_NAMES=(
    "data"
    "experiments"
    "experiments_optimization"
    "gaussian_output"
    "gaussian_output_pruned_policy_30_55"
)
readonly -a PUBLIC_ASSET_URLS=(
    "https://drive.google.com/file/d/1aNse_gijcxVkolD4_PLD4fxXNQfuKkK-/view?usp=drive_link"
    "https://drive.google.com/file/d/1dAUMfyojdSKp2dc5aMhXNUVfTJMj7W76/view?usp=drive_link"
    "https://drive.google.com/file/d/1MMRpFHpN47nhXc3nZxfWwpnnDp2ITCw5/view?usp=drive_link"
    "https://drive.google.com/file/d/1ZtYBj0tEGNLppcSAzt9r-oVUdSAdMHnN/view?usp=drive_link"
    "https://drive.google.com/file/d/1nDpWimKg8hsFaXwzo7MdceGN1HQS3c02/view?usp=drive_link"
)

log() {
    printf '[setup_boba_immersive] %s\n' "$*"
}

die() {
    printf '[setup_boba_immersive] error: %s\n' "$*" >&2
    exit 1
}

usage() {
    cat <<'EOF'
Usage: setup_boba_immersive.sh [options]

Download and configure the complete Boba software stack used by ILLIXR's
boba_immersive plugin. The dedicated Conda environment is named boba-cu132.

Options:
  --install-root DIR       Install repositories and cached tools below DIR.
                           Default: $XDG_DATA_HOME/illixr/boba_immersive, or
                           $HOME/.local/share/illixr/boba_immersive.
  --garden                 Also download and prepare the optional Garden scene.
  --skip-public-assets     Skip the five Boba-Public Required Assets archives.
                           The ILLIXR Lab demo does not consume those archives,
                           but a complete Boba-Public installation does.
  --keep-downloads         Keep Boba-Public zip archives after extraction.
  --dry-run                Print the pinned inputs and planned locations only.
  -h, --help               Show this help.

The script deliberately does not install Steam, SteamVR, or ALVR. The MVP
assumes those host/headset components are already installed and paired.
EOF
}

require_command() {
    local command_name="$1"
    command -v "${command_name}" >/dev/null 2>&1 ||
        die "Required command was not found: ${command_name}"
}

sha256_of() {
    sha256sum "$1" | awk '{print $1}'
}

directory_has_content() {
    local directory="$1"
    [[ -d "${directory}" ]] &&
        [[ -n "$(find "${directory}" -mindepth 1 -print -quit 2>/dev/null)" ]]
}

default_install_root() {
    if [[ -n "${XDG_DATA_HOME:-}" ]]; then
        printf '%s/illixr/boba_immersive\n' "${XDG_DATA_HOME}"
    elif [[ -n "${HOME:-}" ]]; then
        printf '%s/.local/share/illixr/boba_immersive\n' "${HOME}"
    else
        return 1
    fi
}

INSTALL_ROOT="${BOBA_IMMERSIVE_ROOT:-}"
FETCH_GARDEN=0
FETCH_PUBLIC_ASSETS=1
KEEP_DOWNLOADS=0
DRY_RUN=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --install-root)
            [[ $# -ge 2 ]] || die "--install-root requires a directory."
            INSTALL_ROOT="$2"
            shift 2
            ;;
        --garden)
            FETCH_GARDEN=1
            shift
            ;;
        --skip-public-assets)
            FETCH_PUBLIC_ASSETS=0
            shift
            ;;
        --keep-downloads)
            KEEP_DOWNLOADS=1
            shift
            ;;
        --dry-run)
            DRY_RUN=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "Unknown option: $1"
            ;;
    esac
done

if [[ -z "${INSTALL_ROOT}" ]]; then
    INSTALL_ROOT="$(default_install_root)" ||
        die "Neither XDG_DATA_HOME nor HOME is set; pass --install-root."
fi
require_command realpath
INSTALL_ROOT="$(realpath -m -- "${INSTALL_ROOT}")"
[[ "${INSTALL_ROOT}" != "/" ]] || die "The install root cannot be /."
if [[ -n "${HOME:-}" ]]; then
    [[ "${INSTALL_ROOT}" != "$(realpath -m -- "${HOME}")" ]] ||
        die "The install root cannot be HOME itself."
fi

readonly INSTALL_ROOT
readonly BOBA_PUBLIC_ROOT="${INSTALL_ROOT}/Boba-Public"
readonly BOBA_DEMO_ROOT="${INSTALL_ROOT}/Boba-Demo"
readonly DOWNLOAD_ROOT="${INSTALL_ROOT}/downloads"
readonly STATE_ROOT="${INSTALL_ROOT}/.state"
readonly TEMP_ROOT="${INSTALL_ROOT}/.tmp"
readonly GDOWN_ROOT="${INSTALL_ROOT}/.setup-tools/gdown-${GDOWN_VERSION}"

print_plan() {
    log "Install root: ${INSTALL_ROOT}"
    log "Boba-Public: ${BOBA_PUBLIC_REPOSITORY} at ${BOBA_PUBLIC_REF}"
    log "Boba-Demo:   ${BOBA_DEMO_REPOSITORY} at ${BOBA_DEMO_REF}"
    log "Conda environment: ${BOBA_ENVIRONMENT}"
    if [[ ${FETCH_PUBLIC_ASSETS} -eq 1 ]]; then
        local index
        for index in "${!PUBLIC_ASSET_NAMES[@]}"; do
            log "Required Asset ${PUBLIC_ASSET_NAMES[index]}: ${PUBLIC_ASSET_URLS[index]}"
        done
    else
        log "Boba-Public Required Assets: skipped by request"
    fi
    if [[ ${FETCH_GARDEN} -eq 1 ]]; then
        log "Optional Garden scene: download and prepare"
    else
        log "Optional Garden scene: not requested"
    fi
}

if [[ ${DRY_RUN} -eq 1 ]]; then
    print_plan
    exit 0
fi

[[ "$(uname -s)" == "Linux" ]] || die "Boba immersive currently requires Linux."
for required in git curl awk find realpath sha256sum unzip; do
    require_command "${required}"
done

CONDA_BIN="${CONDA_EXE:-}"
if [[ -z "${CONDA_BIN}" ]]; then
    CONDA_BIN="$(command -v conda || true)"
fi
[[ -n "${CONDA_BIN}" && -x "${CONDA_BIN}" ]] ||
    die "Conda was not found. Install or initialize Conda, then rerun this script."
readonly CONDA_BIN

if ! command -v nvidia-smi >/dev/null 2>&1; then
    die "nvidia-smi was not found; a working NVIDIA driver is required."
fi
if ! nvidia-smi >/dev/null 2>&1; then
    die "nvidia-smi could not communicate with the NVIDIA driver."
fi

mkdir -p "${INSTALL_ROOT}" "${DOWNLOAD_ROOT}" "${STATE_ROOT}" "${TEMP_ROOT}"

TEMP_PATHS=()
cleanup_temp_paths() {
    local path
    for path in "${TEMP_PATHS[@]}"; do
        case "${path}" in
            "${TEMP_ROOT}"/*)
                rm -rf -- "${path}"
                ;;
        esac
    done
}
trap cleanup_temp_paths EXIT

expected_remote_matches() {
    local remote="$1"
    local repository="$2"
    local https_without_suffix="${repository%.git}"
    local repository_path="${https_without_suffix#https://github.com/}"
    case "${remote%.git}" in
        "${https_without_suffix}"|"git@github.com:${repository_path}")
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

tracked_checkout_changes() {
    local directory="$1"
    local ignored_paths="${2:-}"
    git -C "${directory}" status --porcelain --untracked-files=no |
        awk -v ignored="${ignored_paths}" '
            BEGIN {
                ignored_count = split(ignored, ignored_list, ":")
            }
            {
                path = substr($0, 4)
                should_ignore = 0
                for (ignored_index = 1; ignored_index <= ignored_count; ++ignored_index) {
                    if (path == ignored_list[ignored_index]) {
                        should_ignore = 1
                    }
                }
                if (!should_ignore) {
                    print
                }
            }
        '
}

patch_boba_app_environment() {
    git -C "${BOBA_DEMO_ROOT}" apply --unidiff-zero <<'PATCH'
diff --git a/boba_app.sh b/boba_app.sh
--- a/boba_app.sh
+++ b/boba_app.sh
@@ -7 +7 @@
-RUNTIME_ENV="phystwin-cu132"
+RUNTIME_ENV="${BOBA_RUNTIME_ENV:-boba-cu132}"
PATCH
}

patch_boba_main_environment() {
    git -C "${BOBA_DEMO_ROOT}" apply --unidiff-zero <<'PATCH'
diff --git a/boba_quest_immersive.py b/boba_quest_immersive.py
--- a/boba_quest_immersive.py
+++ b/boba_quest_immersive.py
@@ -28,7 +28,7 @@ SHARED_TUTORIAL_SLIDES = (
     "interaction_tips.png",
 )
 RUNTIME_ENV_READY_SENTINEL = "BOBA_IMMERSIVE_RUNTIME_READY"
-REQUIRED_RUNTIME_ENV = "phystwin-cu132"
+REQUIRED_RUNTIME_ENV = os.environ.get("BOBA_RUNTIME_ENV", "boba-cu132")
 DEFAULT_CUDA_HOME = "/usr/local/cuda"
 BRIDGE_DEPS_CHECK_SCRIPT = (
     REPO_ROOT / "linux_pose_probe" / "check_boba_immersive_bridge_deps.sh"
@@ -123 +123 @@ def ensure_direct_launch_runtime_env(argv: list[str] | None = None) -> None:
-        "[startup] re-executing with phystwin-cu132 CUDA runtime libraries: "
+        f"[startup] re-executing with {REQUIRED_RUNTIME_ENV} CUDA runtime libraries: "
PATCH
}

patch_gsplat_vendor_environment() {
    git -C "${BOBA_DEMO_ROOT}" apply --unidiff-zero <<'PATCH'
diff --git a/gaussian_splatting/_gsplat_vendor.py b/gaussian_splatting/_gsplat_vendor.py
--- a/gaussian_splatting/_gsplat_vendor.py
+++ b/gaussian_splatting/_gsplat_vendor.py
@@ -13 +13 @@
-EXPECTED_CONDA_ENV = "phystwin-cu132"
+EXPECTED_CONDA_ENV = os.environ.get("BOBA_RUNTIME_ENV", "boba-cu132")
PATCH
}

patch_demo_asset_hint_environment() {
    git -C "${BOBA_DEMO_ROOT}" apply <<'PATCH'
diff --git a/tools/fetch_demo_case_assets.py b/tools/fetch_demo_case_assets.py
--- a/tools/fetch_demo_case_assets.py
+++ b/tools/fetch_demo_case_assets.py
@@ -243,7 +243,7 @@ def resolve_shared_runtime_assets(
             raise DemoAssetValidationError(
                 f"Garden scene assets are not ready: {exc}\n"
                 "Install them once with:\n"
-                "  conda run -n phystwin-cu132 env PYTHONNOUSERSITE=1 "
+                "  conda run -n boba-cu132 env PYTHONNOUSERSITE=1 "
                 "python tools/fetch_demo_case_assets.py --scene garden --fetch"
             ) from exc
         for index, path in enumerate(garden_paths):
PATCH
}

patch_garden_asset_hint_environment() {
    git -C "${BOBA_DEMO_ROOT}" apply <<'PATCH'
diff --git a/qqtt/garden_assets.py b/qqtt/garden_assets.py
--- a/qqtt/garden_assets.py
+++ b/qqtt/garden_assets.py
@@ -132,7 +132,7 @@ def validate_garden_source(repo_root: str | Path) -> Path:
     if not source_path.is_file():
         raise GardenAssetError(
             "Garden source Gaussian is not installed. Run:\n"
-            "  conda run -n phystwin-cu132 env PYTHONNOUSERSITE=1 "
+            "  conda run -n boba-cu132 env PYTHONNOUSERSITE=1 "
             "python tools/fetch_demo_case_assets.py --scene garden --fetch"
         )
     with source_path.open("rb") as handle:
@@ -166,7 +166,7 @@ def validate_garden_quality(
     if not ply_path.is_file() or not metadata_path.is_file():
         raise GardenAssetError(
             f"Garden {quality} runtime assets are missing. Run:\n"
-            "  conda run -n phystwin-cu132 env PYTHONNOUSERSITE=1 "
+            "  conda run -n boba-cu132 env PYTHONNOUSERSITE=1 "
             "python tools/fetch_demo_case_assets.py --scene garden --fetch"
         )
     with ply_path.open("rb") as handle:
PATCH
}

ensure_demo_patched_file() {
    local relative_path="$1"
    local expected_sha="$2"
    local patch_function="$3"
    local target="${BOBA_DEMO_ROOT}/${relative_path}"
    local current_sha
    local original_sha

    [[ -f "${target}" ]] || die "Missing Boba-Demo file required for environment patch: ${target}"
    current_sha="$(sha256_of "${target}")"
    if [[ "${current_sha}" == "${expected_sha}" ]]; then
        return
    fi

    original_sha="$(git -C "${BOBA_DEMO_ROOT}" show "${BOBA_DEMO_REF}:${relative_path}" | sha256sum | awk '{print $1}')"
    [[ "${current_sha}" == "${original_sha}" ]] ||
        die "Boba-Demo file has unexpected modifications: ${target}"

    "${patch_function}"
    current_sha="$(sha256_of "${target}")"
    [[ "${current_sha}" == "${expected_sha}" ]] ||
        die "Environment compatibility patch produced an unexpected file: ${target}"
}

apply_demo_environment_patch() {
    ensure_demo_patched_file "boba_app.sh" "${PATCHED_BOBA_APP_SHA256}" patch_boba_app_environment
    ensure_demo_patched_file "boba_quest_immersive.py" "${PATCHED_BOBA_MAIN_SHA256}" patch_boba_main_environment
    ensure_demo_patched_file "gaussian_splatting/_gsplat_vendor.py" "${PATCHED_GSPLAT_VENDOR_SHA256}" patch_gsplat_vendor_environment
    ensure_demo_patched_file "tools/fetch_demo_case_assets.py" "${PATCHED_DEMO_ASSETS_SHA256}" patch_demo_asset_hint_environment
    ensure_demo_patched_file "qqtt/garden_assets.py" "${PATCHED_GARDEN_ASSETS_SHA256}" patch_garden_asset_hint_environment
    log "Boba-Demo is configured to use Conda environment ${BOBA_ENVIRONMENT}"
}

ensure_checkout() {
    local label="$1"
    local repository="$2"
    local ref="$3"
    local directory="$4"
    local ignored_tracked_path="${5:-}"
    local remote
    local current_ref
    local changes

    if [[ -e "${directory}" && ! -d "${directory}" ]]; then
        die "${label} target exists and is not a directory: ${directory}"
    fi

    if [[ ! -d "${directory}/.git" ]]; then
        if directory_has_content "${directory}"; then
            die "${label} target is nonempty but is not a Git checkout: ${directory}"
        fi
        log "Creating ${label} checkout in ${directory}"
        mkdir -p "${directory}"
        git -C "${directory}" init
        git -C "${directory}" remote add origin "${repository}"
    fi

    remote="$(git -C "${directory}" remote get-url origin 2>/dev/null || true)"
    expected_remote_matches "${remote}" "${repository}" ||
        die "${label} origin is unexpected: ${remote:-missing}"

    changes="$(tracked_checkout_changes "${directory}" "${ignored_tracked_path}")"
    if [[ -n "${changes}" ]]; then
        printf '[setup_boba_immersive] error: %s has tracked local changes; refusing to overwrite them:\n%s\n' \
            "${label}" "${changes}" >&2
        exit 1
    fi

    current_ref="$(git -C "${directory}" rev-parse HEAD 2>/dev/null || true)"
    if [[ "${current_ref}" == "${ref}" ]]; then
        log "${label} is already at the pinned revision ${ref}"
        return
    fi

    log "Fetching ${label} revision ${ref}"
    GIT_LFS_SKIP_SMUDGE=1 git -C "${directory}" fetch \
        --depth=1 --filter=blob:none origin "${ref}"
    GIT_LFS_SKIP_SMUDGE=1 git -C "${directory}" \
        -c advice.detachedHead=false checkout --detach FETCH_HEAD

    current_ref="$(git -C "${directory}" rev-parse HEAD)"
    [[ "${current_ref}" == "${ref}" ]] ||
        die "${label} resolved to ${current_ref}, expected ${ref}."
}

ensure_demo_lfs_asset() {
    local target="${BOBA_DEMO_ROOT}/${DEMO_LFS_PATH}"
    local partial="${DOWNLOAD_ROOT}/boba-demo/sloth.ply.part"
    local actual_sha=""

    if [[ -f "${target}" ]]; then
        actual_sha="$(sha256_of "${target}")"
    fi
    if [[ "${actual_sha}" == "${DEMO_LFS_SHA256}" ]]; then
        log "Boba-Demo Sloth Git LFS payload is already present"
        return
    fi

    mkdir -p "$(dirname -- "${partial}")"
    if [[ -f "${partial}" ]] &&
       [[ "$(sha256_of "${partial}")" == "${DEMO_LFS_SHA256}" ]]; then
        log "Using the completed cached Sloth payload"
    else
        log "Downloading the pinned Boba-Demo Sloth payload"
        curl --fail --location \
            --retry 5 --retry-delay 2 --retry-all-errors \
            --continue-at - \
            --output "${partial}" \
            "${DEMO_LFS_URL}"
    fi

    actual_sha="$(sha256_of "${partial}")"
    [[ "${actual_sha}" == "${DEMO_LFS_SHA256}" ]] ||
        die "Sloth payload checksum mismatch. Partial file retained at ${partial}"
    mkdir -p "$(dirname -- "${target}")"
    mv -f -- "${partial}" "${target}"
}

have_conda_environment() {
    "${CONDA_BIN}" env list |
        awk -v wanted="${BOBA_ENVIRONMENT}" '
            $1 == wanted { found = 1 }
            END { exit !found }
        '
}

run_in_boba_environment() {
    "${CONDA_BIN}" run --no-capture-output -n "${BOBA_ENVIRONMENT}" \
        env PYTHONNOUSERSITE=1 "$@"
}

ensure_conda_environment() {
    if have_conda_environment; then
        log "Reusing Conda environment ${BOBA_ENVIRONMENT}"
    else
        log "Creating Conda environment ${BOBA_ENVIRONMENT}"
        "${CONDA_BIN}" env create \
            --name "${BOBA_ENVIRONMENT}" \
            --file "${BOBA_PUBLIC_ROOT}/env_install/phystwin-cu132.yml"
    fi

    if ! run_in_boba_environment python -c \
        'import sys, torch
assert sys.version_info[:2] == (3, 10), sys.version
assert torch.__version__ == "2.12.1+cu132", torch.__version__
assert torch.version.cuda == "13.2", torch.version.cuda'; then
        die "Existing ${BOBA_ENVIRONMENT} does not match Boba's pinned Python/Torch/CUDA stack. This script will not overwrite a mismatched named environment."
    fi
}

extensions_are_ready() {
    local state_file="${STATE_ROOT}/cuda_extensions"
    [[ -f "${state_file}" ]] || return 1
    [[ "$(sed -n '1p' "${state_file}")" == "${BOBA_PUBLIC_REF}" ]] || return 1
    [[ "$(sed -n '2p' "${state_file}")" == "${BOBA_PUBLIC_ROOT}" ]] || return 1
    [[ "$(sed -n '3p' "${state_file}")" == "${BOBA_ENVIRONMENT}" ]] || return 1
    run_in_boba_environment python -c \
        'import fused_ssim_cuda, pycuda.gl, simple_knn._C' \
        >/dev/null 2>&1
}

ensure_cuda_extensions() {
    local state_file="${STATE_ROOT}/cuda_extensions"
    local state_temp="${STATE_ROOT}/cuda_extensions.tmp"

    if extensions_are_ready; then
        log "Boba CUDA extensions are already built and verified"
        return
    fi

    log "Building and verifying Boba CUDA extensions"
    (
        cd "${BOBA_PUBLIC_ROOT}"
        run_in_boba_environment env "BOBA_CUDA_ENV_NAME=${BOBA_ENVIRONMENT}" \
            bash ./env_install/build_cuda13_extensions.sh
    )

    printf '%s\n%s\n%s\n' \
        "${BOBA_PUBLIC_REF}" \
        "${BOBA_PUBLIC_ROOT}" \
        "${BOBA_ENVIRONMENT}" \
        >"${state_temp}"
    mv -f -- "${state_temp}" "${state_file}"
}

ensure_gdown() {
    if run_in_boba_environment env "PYTHONPATH=${GDOWN_ROOT}" python -c \
        'import gdown
from importlib.metadata import version
assert version("gdown") == "6.1.0"' \
        >/dev/null 2>&1; then
        log "gdown ${GDOWN_VERSION} is already available in the setup tool cache"
        return
    fi

    log "Installing isolated gdown ${GDOWN_VERSION} setup tool"
    mkdir -p "${GDOWN_ROOT}"
    run_in_boba_environment python -m pip install \
        --disable-pip-version-check \
        --no-input \
        --upgrade \
        --target "${GDOWN_ROOT}" \
        "gdown==${GDOWN_VERSION}"
}

run_gdown() {
    run_in_boba_environment env "PYTHONPATH=${GDOWN_ROOT}" \
        python -m gdown "$@"
}

remove_cached_archive() {
    local archive="$1"
    [[ ${KEEP_DOWNLOADS} -eq 0 ]] || return
    case "${archive}" in
        "${DOWNLOAD_ROOT}"/boba-public/*.zip)
            rm -f -- "${archive}"
            ;;
        *)
            die "Refusing to remove an unexpected archive path: ${archive}"
            ;;
    esac
}

install_public_asset() {
    local asset_name="$1"
    local asset_url="$2"
    local target="${BOBA_PUBLIC_ROOT}/${asset_name}"
    local archive="${DOWNLOAD_ROOT}/boba-public/${asset_name}.zip"
    local staging
    local payload
    local -a candidates=()

    if directory_has_content "${target}"; then
        log "Boba-Public asset ${asset_name} is already present"
        return
    fi
    if [[ -e "${target}" || -L "${target}" ]]; then
        die "Asset target exists but is empty or unusable: ${target}"
    fi

    mkdir -p "$(dirname -- "${archive}")"
    log "Downloading Boba-Public Required Asset ${asset_name}"
    run_gdown "${asset_url}" --continue --output "${archive}"
    [[ -s "${archive}" ]] || die "Downloaded archive is empty: ${archive}"
    unzip -tqq "${archive}" ||
        die "Downloaded asset is not a valid zip archive: ${archive}"

    staging="$(mktemp -d "${TEMP_ROOT}/${asset_name}.XXXXXX")"
    TEMP_PATHS+=("${staging}")
    log "Extracting ${asset_name}.zip"
    unzip -q "${archive}" -d "${staging}"

    payload="${staging}/${asset_name}"
    if [[ ! -d "${payload}" ]]; then
        mapfile -t candidates < <(
            find "${staging}" -mindepth 1 -maxdepth 4 \
                -type d -name "${asset_name}" -print
        )
        if [[ ${#candidates[@]} -eq 1 ]]; then
            payload="${candidates[0]}"
        elif [[ ${#candidates[@]} -eq 0 ]]; then
            payload="${staging}"
        else
            die "Archive contains multiple ${asset_name} directories: ${archive}"
        fi
    fi

    mv -- "${payload}" "${target}"
    directory_has_content "${target}" ||
        die "Extracted asset directory is empty: ${target}"
    printf '%s\n' "${asset_url}" >"${STATE_ROOT}/asset-${asset_name}"
    remove_cached_archive "${archive}"
}

install_public_assets() {
    local index
    ensure_gdown
    for index in "${!PUBLIC_ASSET_NAMES[@]}"; do
        install_public_asset \
            "${PUBLIC_ASSET_NAMES[index]}" \
            "${PUBLIC_ASSET_URLS[index]}"
    done
}

install_demo_dependencies() {
    log "Installing Boba-Demo's pinned add-on requirements"
    (
        cd "${BOBA_DEMO_ROOT}"
        run_in_boba_environment python -m pip install \
            --disable-pip-version-check \
            --no-input \
            -r requirements-demo.txt
    )

    log "Validating Boba-Demo's vendored gsplat runtime"
    (
        cd "${BOBA_DEMO_ROOT}"
        run_in_boba_environment env "BOBA_RUNTIME_ENV=${BOBA_ENVIRONMENT}" \
            python -c 'import gaussian_splatting._gsplat_vendor'
    )

    log "Validating the packaged Rope, Sloth, and Lab assets"
    (
        cd "${BOBA_DEMO_ROOT}"
        run_in_boba_environment python tools/fetch_demo_case_assets.py
    )

    if [[ ${FETCH_GARDEN} -eq 1 ]]; then
        log "Downloading and preparing the optional Garden scene"
        (
            cd "${BOBA_DEMO_ROOT}"
            run_in_boba_environment python tools/fetch_demo_case_assets.py \
                --scene garden --fetch
        )
    fi
}

print_plan
ensure_checkout \
    "Boba-Public" \
    "${BOBA_PUBLIC_REPOSITORY}" \
    "${BOBA_PUBLIC_REF}" \
    "${BOBA_PUBLIC_ROOT}"
ensure_checkout \
    "Boba-Demo" \
    "${BOBA_DEMO_REPOSITORY}" \
    "${BOBA_DEMO_REF}" \
    "${BOBA_DEMO_ROOT}" \
    "${DEMO_MANAGED_PATHS}"
apply_demo_environment_patch
ensure_demo_lfs_asset
ensure_conda_environment
ensure_cuda_extensions
if [[ ${FETCH_PUBLIC_ASSETS} -eq 1 ]]; then
    install_public_assets
fi
install_demo_dependencies

log "Boba immersive setup is complete."
log "Launcher: ${BOBA_DEMO_ROOT}/boba_app.sh"
DISCOVERY_ROOT="$(default_install_root || true)"
if [[ -z "${DISCOVERY_ROOT}" ]] ||
   [[ "${INSTALL_ROOT}" != "$(realpath -m -- "${DISCOVERY_ROOT}")" ]]; then
    log "Set BOBA_IMMERSIVE_ROOT=${INSTALL_ROOT} when launching ILLIXR."
else
    log "ILLIXR will discover this default installation automatically."
fi
