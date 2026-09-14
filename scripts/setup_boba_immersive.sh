#!/usr/bin/env bash
set -Eeuo pipefail

# Pin every remote input so repeated installs produce the same Boba runtime.
# Checksums below also make the small compatibility patches fail closed if the
# corresponding upstream file changes unexpectedly.
readonly BOBA_PUBLIC_REPOSITORY="https://github.com/jianxiapyh/Boba-Public.git"
readonly BOBA_PUBLIC_REF="612d22a74c2d54e3f20d2c197182090a22a20494"
readonly BOBA_DEMO_REPOSITORY="https://github.com/jianxiapyh/Boba-Demo.git"
readonly BOBA_DEMO_REF="684d3c2d7fc0cd4c5ba1202748cadf6584d2db01"
readonly BOBA_ENVIRONMENT="boba-cu132"
readonly GDOWN_VERSION="6.1.0"

readonly DEMO_LFS_PATH="assets/sloth/sloth.ply"
readonly DEMO_LFS_SHA256="fc0301db3e5fd077d153e3bb2d68cf609db1ebc6968932101f34d731b6aec5d2"
readonly DEMO_LFS_URL="https://media.githubusercontent.com/media/jianxiapyh/Boba-Demo/${BOBA_DEMO_REF}/${DEMO_LFS_PATH}"
readonly DEMO_MANAGED_PATHS="${DEMO_LFS_PATH}:boba_app.sh:boba_quest_immersive.py:gaussian_splatting/_gsplat_vendor.py:tools/fetch_demo_case_assets.py:qqtt/garden_assets.py:gaussian_splatting/cuda_linalg.py:qqtt/quest_display.py:test/test_cuda_linalg_backend.py"

readonly PUBLIC_MANAGED_PATHS="env_install/build_cuda13_extensions.sh:gaussian_splatting/cuda_linalg.py:gaussian_splatting/_gsplat_vendor.py"

readonly PATCHED_BOBA_APP_SHA256="e7cde1ef6356a089cf92418fead64ee60fbcf8579c0e86685cb16d8b12dc9d55"
readonly PATCHED_BOBA_MAIN_SHA256="7c2e488e3530c37fdc4512e06ff72085d5e1ef64242d4a37319a4c93e80fad82"
readonly PATCHED_GSPLAT_VENDOR_SHA256="9800963458fb8cef5fd6acc0033901466ca91e137e572b0fecfa86c074c8ae44"
readonly PATCHED_DEMO_ASSETS_SHA256="8356763064d2993a4f1bd82c59ded2ee6c6cf3abbb138dfd8608c4024ccc97f9"
readonly PATCHED_GARDEN_ASSETS_SHA256="3431640e18d85f8ccd38b8501fc9e7ed3ed185b75bea76ffad2f4ac7d7b6803d"
readonly PATCHED_CUDA_LINALG_SHA256="b0dd5a4a21a933c5a29219ff81be8d570456fb28544d550e1516afb00a508fca"
readonly PATCHED_QUEST_DISPLAY_SHA256="8d46faa96b3737fd23ee3c40efdd0a41096928fe498caf1a93ad45ca2351ed94"
readonly PATCHED_PUBLIC_GSPLAT_SHA256="cc540b95d870db5ceb5ebaa08a93473a3d19ba24e79fb78072c39152a6acf317"
readonly PATCHED_PUBLIC_BUILDER_SHA256="18d0911339c713137d1fcf7ad19f7bff0141ebf9e60c9fca7cf2e5ef04ab1dc5"

readonly PATCHED_RUNTIME_TEST_SHA256="8013f4d838ee55189e6aed3716413e7e66540f87bbe2f2b4038f1eca023783a4"

# These are the five archives linked from Boba-Public's Required Assets
# section. Their array indices intentionally form name/URL pairs.
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

# ---- Common command-line helpers -------------------------------------------

# Prefix user-facing progress so setup output is distinguishable from output
# emitted by Conda, pip, Git, and the Boba validation tools.
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

The script deliberately does not install a headset transport. For native Quest
streaming, install ILLIXRApp with scripts/install_quest_app.sh. SteamVR and ALVR
are needed only by the legacy boba_quest profile.
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

# ---- Option parsing and installation layout --------------------------------

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

# Print both the immutable source revisions and optional download choices. This
# is also the complete output of --dry-run, which performs no filesystem writes.
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

# ---- Host prerequisite validation ------------------------------------------

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

# Temporary extraction directories are registered as they are created. Limit
# cleanup to TEMP_ROOT so even malformed archive contents cannot broaden it.
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

# ---- Reproducible upstream checkouts ---------------------------------------

# Accept the HTTPS and SSH spellings of a pinned GitHub repository, but reject
# an existing checkout that points at a different upstream project.
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

# Return tracked changes that setup is not expected to manage. The ignored list
# contains the LFS payload and the checksum-guarded compatibility patch targets.
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

# Boba-Demo historically named its original development environment directly.
# These narrowly scoped patches make the runtime name configurable and align the
# setup instructions with the dedicated boba-cu132 environment created here.
patch_boba_app_environment() {
    git -C "${BOBA_DEMO_ROOT}" apply --unidiff-zero <<'PATCH'
diff --git a/boba_app.sh b/boba_app.sh
index 43d5641..d9829b8 100755
--- a/boba_app.sh
+++ b/boba_app.sh
@@ -7 +7 @@ cd "${REPO_ROOT}"
-RUNTIME_ENV="phystwin-cu132"
+RUNTIME_ENV="${BOBA_RUNTIME_ENV:-boba-cu132}"
PATCH
}

patch_boba_main_environment() {
    git -C "${BOBA_DEMO_ROOT}" apply --unidiff-zero <<'PATCH'
diff --git a/boba_quest_immersive.py b/boba_quest_immersive.py
index 5c3fad4..8e089f8 100644
--- a/boba_quest_immersive.py
+++ b/boba_quest_immersive.py
@@ -41 +41 @@ RUNTIME_ENV_READY_SENTINEL = "BOBA_IMMERSIVE_RUNTIME_READY"
-REQUIRED_RUNTIME_ENV = "phystwin-cu132"
+REQUIRED_RUNTIME_ENV = os.environ.get("BOBA_RUNTIME_ENV", "boba-cu132")
@@ -133 +133 @@ def ensure_direct_launch_runtime_env(argv: list[str] | None = None) -> None:
-        "[startup] re-executing with phystwin-cu132 CUDA runtime libraries: "
+        f"[startup] re-executing with {REQUIRED_RUNTIME_ENV} CUDA runtime libraries: "
PATCH
}

patch_gsplat_vendor_environment() {
    git -C "${BOBA_DEMO_ROOT}" apply --unidiff-zero <<'PATCH'
diff --git a/gaussian_splatting/_gsplat_vendor.py b/gaussian_splatting/_gsplat_vendor.py
index e3c099d..b2cc140 100644
--- a/gaussian_splatting/_gsplat_vendor.py
+++ b/gaussian_splatting/_gsplat_vendor.py
@@ -16 +16 @@ import torch
-EXPECTED_CONDA_ENV = "phystwin-cu132"
+EXPECTED_CONDA_ENV = os.environ.get("BOBA_RUNTIME_ENV", "boba-cu132")
PATCH
}

patch_demo_asset_hint_environment() {
    git -C "${BOBA_DEMO_ROOT}" apply --unidiff-zero <<'PATCH'
diff --git a/tools/fetch_demo_case_assets.py b/tools/fetch_demo_case_assets.py
index ea8ce53..17e1f5a 100755
--- a/tools/fetch_demo_case_assets.py
+++ b/tools/fetch_demo_case_assets.py
@@ -251 +251 @@ def resolve_shared_runtime_assets(
-                "  conda run -n phystwin-cu132 env PYTHONNOUSERSITE=1 "
+                "  conda run -n boba-cu132 env PYTHONNOUSERSITE=1 "
PATCH
}

patch_garden_asset_hint_environment() {
    git -C "${BOBA_DEMO_ROOT}" apply --unidiff-zero <<'PATCH'
diff --git a/qqtt/garden_assets.py b/qqtt/garden_assets.py
index 9db464d..6a7bdac 100644
--- a/qqtt/garden_assets.py
+++ b/qqtt/garden_assets.py
@@ -135 +135 @@ def validate_garden_source(repo_root: str | Path) -> Path:
-            "  conda run -n phystwin-cu132 env PYTHONNOUSERSITE=1 "
+            "  conda run -n boba-cu132 env PYTHONNOUSERSITE=1 "
@@ -169 +169 @@ def validate_garden_quality(
-            "  conda run -n phystwin-cu132 env PYTHONNOUSERSITE=1 "
+            "  conda run -n boba-cu132 env PYTHONNOUSERSITE=1 "
PATCH
}

patch_demo_linalg_environment() {
    git -C "${BOBA_DEMO_ROOT}" apply --unidiff-zero <<'PATCH'
diff --git a/gaussian_splatting/cuda_linalg.py b/gaussian_splatting/cuda_linalg.py
index 7ed3a83..862c97c 100644
--- a/gaussian_splatting/cuda_linalg.py
+++ b/gaussian_splatting/cuda_linalg.py
@@ -2,0 +3 @@
+import os
@@ -6 +7 @@ from pathlib import Path
-EXPECTED_CONDA_ENV = "phystwin-cu132"
+EXPECTED_CONDA_ENV = os.environ.get("BOBA_RUNTIME_ENV", "boba-cu132")
@@ -14,2 +15,2 @@ def require_runtime(torch_module=None):
-            "Boba requires the 'phystwin-cu132' conda environment. "
-            f"Python prefix: {sys.prefix!r}. Run: conda activate phystwin-cu132"
+            f"Boba requires the {EXPECTED_CONDA_ENV!r} conda environment. "
+            f"Python prefix: {sys.prefix!r}. Run: conda activate {EXPECTED_CONDA_ENV}"
@@ -27 +28 @@ def require_runtime(torch_module=None):
-            f"phystwin-cu132; found torch.version.cuda={build!r}. "
+            f"{EXPECTED_CONDA_ENV}; found torch.version.cuda={build!r}. "
PATCH
}

patch_demo_ring_generation() {
    git -C "${BOBA_DEMO_ROOT}" apply --unidiff-zero <<'PATCH'
diff --git a/qqtt/quest_display.py b/qqtt/quest_display.py
index 72ea5d4..8cb7bba 100644
--- a/qqtt/quest_display.py
+++ b/qqtt/quest_display.py
@@ -544,0 +545,2 @@ class OpenXRFramePanelMirror:
+        # Invalidate the old generation before recycling its pixel storage.
+        self._write_frame_slot_metadata(slot=slot, frame_id=0)
@@ -1079,0 +1082,2 @@ class OpenXRFramePanelMirror:
+        # Readers must reject the slot for the entire asynchronous overwrite.
+        self._write_frame_slot_metadata(slot=slot, frame_id=0)
PATCH
}

patch_public_linalg_environment() {
    git -C "${BOBA_PUBLIC_ROOT}" apply --unidiff-zero <<'PATCH'
diff --git a/gaussian_splatting/cuda_linalg.py b/gaussian_splatting/cuda_linalg.py
index 7ed3a83..862c97c 100644
--- a/gaussian_splatting/cuda_linalg.py
+++ b/gaussian_splatting/cuda_linalg.py
@@ -2,0 +3 @@
+import os
@@ -6 +7 @@ from pathlib import Path
-EXPECTED_CONDA_ENV = "phystwin-cu132"
+EXPECTED_CONDA_ENV = os.environ.get("BOBA_RUNTIME_ENV", "boba-cu132")
@@ -14,2 +15,2 @@ def require_runtime(torch_module=None):
-            "Boba requires the 'phystwin-cu132' conda environment. "
-            f"Python prefix: {sys.prefix!r}. Run: conda activate phystwin-cu132"
+            f"Boba requires the {EXPECTED_CONDA_ENV!r} conda environment. "
+            f"Python prefix: {sys.prefix!r}. Run: conda activate {EXPECTED_CONDA_ENV}"
@@ -27 +28 @@ def require_runtime(torch_module=None):
-            f"phystwin-cu132; found torch.version.cuda={build!r}. "
+            f"{EXPECTED_CONDA_ENV}; found torch.version.cuda={build!r}. "
PATCH
}

patch_public_gsplat_environment() {
    git -C "${BOBA_PUBLIC_ROOT}" apply --unidiff-zero <<'PATCH'
diff --git a/gaussian_splatting/_gsplat_vendor.py b/gaussian_splatting/_gsplat_vendor.py
index 7bea322..895c428 100644
--- a/gaussian_splatting/_gsplat_vendor.py
+++ b/gaussian_splatting/_gsplat_vendor.py
@@ -11,2 +11,2 @@ from .cuda_linalg import require_runtime
-EXPECTED_CONDA_ENV = "phystwin-cu132"
-SUPPORTED_CONDA_ENVS = {"phystwin-cu132"}
+EXPECTED_CONDA_ENV = os.environ.get("BOBA_RUNTIME_ENV", "boba-cu132")
+SUPPORTED_CONDA_ENVS = {EXPECTED_CONDA_ENV}
@@ -20 +20 @@ def _install_hint() -> str:
-        "Activate phystwin-cu132 and run "
+        f"Activate {EXPECTED_CONDA_ENV} and run "
@@ -97 +97 @@ def import_gsplat():
-            "Boba could not import gsplat from the active phystwin-cu132 environment.\n"
+            f"Boba could not import gsplat from the active {EXPECTED_CONDA_ENV} environment.\n"
PATCH
}

patch_public_builder_environment() {
    git -C "${BOBA_PUBLIC_ROOT}" apply --unidiff-zero <<'PATCH'
diff --git a/env_install/build_cuda13_extensions.sh b/env_install/build_cuda13_extensions.sh
index de93159..82fa167 100755
--- a/env_install/build_cuda13_extensions.sh
+++ b/env_install/build_cuda13_extensions.sh
@@ -8 +8 @@ RUNTIME_HOOK_ROOT="${REPO_ROOT}/env_install/conda"
-EXPECTED_ENV="phystwin-cu132"
+EXPECTED_ENV="${BOBA_CUDA_ENV_NAME:-boba-cu132}"
@@ -69 +69 @@ BOBA_CUDA_DRIVER_LIBRARY="$({ ldconfig -p 2>/dev/null || true; } | awk '
-  $1 == "libcuda.so" && $0 ~ /x86-64/ && !found { print $NF; found = 1 }
+  ($1 == "libcuda.so" || $1 == "libcuda.so.1") && $0 ~ /x86-64/ && !found { print $NF; found = 1 }
@@ -75 +75,3 @@ fi
-BOBA_CUDA_DRIVER_DIR="$(dirname "${BOBA_CUDA_DRIVER_LIBRARY}")"
+# Provide the linker name inside the Conda environment without changing the host driver.
+BOBA_CUDA_DRIVER_DIR="${ACTIVE_PREFIX}/lib"
+ln -sfn "${BOBA_CUDA_DRIVER_LIBRARY}" "${BOBA_CUDA_DRIVER_DIR}/libcuda.so"
PATCH
}

patch_demo_runtime_policy_test() {
    git -C "${BOBA_DEMO_ROOT}" apply --unidiff-zero <<'PATCH'
--- a/test/test_cuda_linalg_backend.py
+++ b/test/test_cuda_linalg_backend.py
@@ -6 +6 @@
-from gaussian_splatting.cuda_linalg import configure_linalg_backend, require_runtime
+from gaussian_splatting.cuda_linalg import EXPECTED_CONDA_ENV, configure_linalg_backend, require_runtime
@@ -13 +13 @@
-                os.environ, {"CONDA_DEFAULT_ENV": "phystwin-cu132"}
+                os.environ, {"CONDA_DEFAULT_ENV": EXPECTED_CONDA_ENV}
@@ -15 +15 @@
-                with self.assertRaisesRegex(RuntimeError, "conda activate phystwin-cu132"):
+                with self.assertRaisesRegex(RuntimeError, f"conda activate {EXPECTED_CONDA_ENV}"):
@@ -19 +19 @@
-        with patch("sys.prefix", "/tmp/envs/phystwin-cu132"):
+        with patch("sys.prefix", f"/tmp/envs/{EXPECTED_CONDA_ENV}"):
@@ -32 +32 @@
-        with patch("sys.prefix", "/tmp/envs/phystwin-cu132"), patch.dict(
+        with patch("sys.prefix", f"/tmp/envs/{EXPECTED_CONDA_ENV}"), patch.dict(
PATCH
}

# Apply a compatibility patch only to an unmodified pinned upstream file, then
# verify the exact result. This avoids silently overwriting user or upstream
# edits when setup is rerun.
ensure_runtime_patched_file() {
    local relative_path="$1"
    local expected_sha="$2"
    local patch_function="$3"
    local directory="${4:-${BOBA_DEMO_ROOT}}"
    local ref="${5:-${BOBA_DEMO_REF}}"
    local target="${directory}/${relative_path}"
    local current_sha
    local original_sha

    [[ -f "${target}" ]] || die "Missing Boba file required for compatibility patch: ${target}"
    current_sha="$(sha256_of "${target}")"
    if [[ "${current_sha}" == "${expected_sha}" ]]; then
        return
    fi

    original_sha="$(git -C "${directory}" show "${ref}:${relative_path}" | sha256sum | awk '{print $1}')"
    [[ "${current_sha}" == "${original_sha}" ]] ||
        die "Boba file has unexpected modifications: ${target}"

    "${patch_function}"
    current_sha="$(sha256_of "${target}")"
    [[ "${current_sha}" == "${expected_sha}" ]] ||
        die "Compatibility patch produced an unexpected file: ${target}"
}

# Configure every Boba-Demo entry point that either launches or documents the
# Python/CUDA environment used by the ILLIXR integration.
apply_demo_compatibility_patches() {
    ensure_runtime_patched_file "boba_app.sh" "${PATCHED_BOBA_APP_SHA256}" patch_boba_app_environment
    ensure_runtime_patched_file "boba_quest_immersive.py" "${PATCHED_BOBA_MAIN_SHA256}" patch_boba_main_environment
    ensure_runtime_patched_file "gaussian_splatting/_gsplat_vendor.py" "${PATCHED_GSPLAT_VENDOR_SHA256}" patch_gsplat_vendor_environment
    ensure_runtime_patched_file "tools/fetch_demo_case_assets.py" "${PATCHED_DEMO_ASSETS_SHA256}" patch_demo_asset_hint_environment
    ensure_runtime_patched_file "qqtt/garden_assets.py" "${PATCHED_GARDEN_ASSETS_SHA256}" patch_garden_asset_hint_environment
    ensure_runtime_patched_file "gaussian_splatting/cuda_linalg.py" "${PATCHED_CUDA_LINALG_SHA256}" patch_demo_linalg_environment
    ensure_runtime_patched_file "qqtt/quest_display.py" "${PATCHED_QUEST_DISPLAY_SHA256}" patch_demo_ring_generation
    ensure_runtime_patched_file "test/test_cuda_linalg_backend.py" "${PATCHED_RUNTIME_TEST_SHA256}" patch_demo_runtime_policy_test
    log "Boba-Demo compatibility patches verified for ${BOBA_ENVIRONMENT}"
}

apply_public_compatibility_patches() {
    ensure_runtime_patched_file "gaussian_splatting/cuda_linalg.py" "${PATCHED_CUDA_LINALG_SHA256}" patch_public_linalg_environment "${BOBA_PUBLIC_ROOT}" "${BOBA_PUBLIC_REF}"
    ensure_runtime_patched_file "gaussian_splatting/_gsplat_vendor.py" "${PATCHED_PUBLIC_GSPLAT_SHA256}" patch_public_gsplat_environment "${BOBA_PUBLIC_ROOT}" "${BOBA_PUBLIC_REF}"
    ensure_runtime_patched_file "env_install/build_cuda13_extensions.sh" "${PATCHED_PUBLIC_BUILDER_SHA256}" patch_public_builder_environment "${BOBA_PUBLIC_ROOT}" "${BOBA_PUBLIC_REF}"
    log "Boba-Public compatibility patches verified for ${BOBA_ENVIRONMENT}"
}

# Preserve known installer patches when changing revisions. Unknown edits,
# including staged changes, are never reset. Unchanged LFS blobs stay hydrated.
prepare_checkout_upgrade() {
    local directory="$1" ref="$2" managed_paths="$3"
    local path sha backup
    local -a paths changed_paths=()
    IFS=: read -r -a paths <<<"${managed_paths}"
    git -C "${directory}" diff --cached --quiet ||
        die "Checkout has staged changes; use a separate --install-root: ${directory}"
    for path in "${paths[@]}"; do
        [[ -n "${path}" ]] || continue
        git -C "${directory}" diff --quiet -- "${path}" && continue
        if [[ "${path}" == "${DEMO_LFS_PATH}" ]] &&
           [[ "$(git -C "${directory}" rev-parse "HEAD:${path}")" == \
              "$(git -C "${directory}" rev-parse "${ref}:${path}")" ]]; then
            continue
        fi
        [[ -f "${directory}/${path}" && ! -L "${directory}/${path}" ]] ||
            die "Managed file was removed or replaced: ${directory}/${path}"
        sha="$(sha256_of "${directory}/${path}")"
        # Exact compatibility-patch outputs from the previous installer.
        case "${path}:${sha}" in
            boba_app.sh:d9193820fb1c79ee87d22389dbef9624d5631d5a4ddc93c116f74a0004a6b9b2|\
            boba_quest_immersive.py:bb486ed6643813a2346e5e624dd9a07fa50b76a5d8785ebf8bff6f891a9619d5|\
            gaussian_splatting/_gsplat_vendor.py:c4e1b07377bb0993c321cdb6cb5ce0bf2b1b0ecdafb941bd0172500ef8fdc3e0|\
            tools/fetch_demo_case_assets.py:0f42bf6eef01f341f9f9dc6a67f69eef24eefb6879da355b74560b8d8ed6fdde|\
            qqtt/garden_assets.py:3431640e18d85f8ccd38b8501fc9e7ed3ed185b75bea76ffad2f4ac7d7b6803d|\
            "${DEMO_LFS_PATH}:${DEMO_LFS_SHA256}")
                changed_paths+=("${path}")
                ;;
            *)
                die "Managed file has custom edits; use a separate --install-root: ${directory}/${path}"
                ;;
        esac
    done
    [[ ${#changed_paths[@]} -gt 0 ]] || return 0
    mkdir -p "${STATE_ROOT}/checkout-backups"
    backup="$(mktemp -d "${STATE_ROOT}/checkout-backups/$(basename "${directory}").XXXXXX")"
    git -C "${directory}" rev-parse HEAD >"${backup}/revision"
    for path in "${changed_paths[@]}"; do
        mkdir -p "${backup}/$(dirname "${path}")"
        cp -p -- "${directory}/${path}" "${backup}/${path}"
    done
    log "Backed up previous installer patches to ${backup}"
    git -C "${directory}" restore --worktree -- "${changed_paths[@]}"
}

# Create or update a checkout to exactly `ref`. Local tracked changes cause a
# hard failure rather than an implicit reset; large Git LFS blobs are handled by
# the separately verified download path below.
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

    if [[ ! -e "${directory}/.git" ]]; then
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
    prepare_checkout_upgrade "${directory}" "${ref}" "${ignored_tracked_path}"
    GIT_LFS_SKIP_SMUDGE=1 git -C "${directory}" \
        -c advice.detachedHead=false checkout --detach FETCH_HEAD

    current_ref="$(git -C "${directory}" rev-parse HEAD)"
    [[ "${current_ref}" == "${ref}" ]] ||
        die "${label} resolved to ${current_ref}, expected ${ref}."
}

# Fetch the one required LFS object without requiring git-lfs on the host. A
# resumable partial download is retained on checksum failure for the next run.
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

# ---- Isolated Python and CUDA runtime ---------------------------------------

# Test by environment name instead of installation prefix so this also works
# with non-default Conda roots.
have_conda_environment() {
    "${CONDA_BIN}" env list |
        awk -v wanted="${BOBA_ENVIRONMENT}" '
            $1 == wanted { found = 1 }
            END { exit !found }
        '
}

# Prevent packages from the user's site directory from leaking into validation
# or setup commands run in the dedicated Boba environment.
run_in_boba_environment() {
    env -u CONDA_PREFIX -u CONDA_DEFAULT_ENV -u CONDA_SHLVL \
        "${CONDA_BIN}" run --no-capture-output -n "${BOBA_ENVIRONMENT}" \
        env PYTHONNOUSERSITE=1 "BOBA_RUNTIME_ENV=${BOBA_ENVIRONMENT}" "$@"
}

# Reuse boba-cu132 only when it has the exact Python, PyTorch, and CUDA versions
# required by Boba; a mismatched environment is left untouched.
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

# A small state record avoids rebuilding CUDA extensions on every run. Imports
# still verify that the recorded build is loadable in the current environment.
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

# Build extensions through Boba-Public's pinned installer, then atomically
# publish the revision/path/environment tuple used for the successful build.
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

# ---- Required asset installation -------------------------------------------

# Install gdown into a setup-private target directory so downloading assets does
# not mutate Boba's otherwise pinned runtime dependencies.
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

# Delete only archives owned by this installer and only when the user has not
# requested --keep-downloads.
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

# Download and validate one Required Asset archive, extract it in TEMP_ROOT, and
# move the resolved payload into place only after extraction succeeds.
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

# Install Boba-Demo's add-on packages and exercise each runtime asset/backend so
# setup failures are reported now rather than during an ILLIXR session.
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

# ---- End-to-end setup orchestration ----------------------------------------

# Source checkout precedes environment and asset setup because both consume
# files from the pinned repositories. Each step is safe to rerun.
print_plan
ensure_checkout \
    "Boba-Public" \
    "${BOBA_PUBLIC_REPOSITORY}" \
    "${BOBA_PUBLIC_REF}" \
    "${BOBA_PUBLIC_ROOT}" \
    "${PUBLIC_MANAGED_PATHS}"
ensure_checkout \
    "Boba-Demo" \
    "${BOBA_DEMO_REPOSITORY}" \
    "${BOBA_DEMO_REF}" \
    "${BOBA_DEMO_ROOT}" \
    "${DEMO_MANAGED_PATHS}"
apply_public_compatibility_patches
apply_demo_compatibility_patches
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
