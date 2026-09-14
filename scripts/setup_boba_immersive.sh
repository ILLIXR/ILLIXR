#!/usr/bin/env bash
set -Eeuo pipefail

# The companion repository owns the runtime, assets, and CUDA environment.
# Keep this immutable revision in sync with the integration tested by this PR.
readonly BOBA_REF="ff82409f25117ff6657dc6e81f6e9530d3111abb"
REPOSITORY="git@github.com:ILLIXR/Boba-ILLIXR.git"
INSTALL_ROOT="${XDG_DATA_HOME:-${HOME:?HOME or XDG_DATA_HOME must be set}/.local/share}/illixr/boba_immersive"
SOURCE_DIR=""
INSTALL_ROOT_EXPLICIT=0
DRY_RUN=0
SETUP_ARGS=()

log() { printf '[setup_boba_immersive] %s\n' "$*"; }
die() { log "error: $*" >&2; exit 1; }
usage() {
    cat <<'HELP'
Usage: setup_boba_immersive.sh [options]

Install ILLIXR/Boba-ILLIXR and configure its boba-cu132 environment.
The companion repository is private; authenticated Git and Git LFS access
are required. SSH is the default; an HTTPS credential helper is also supported.

  --install-root DIR   Create the pinned Boba-ILLIXR checkout below DIR.
                       Default: $XDG_DATA_HOME/illixr/boba_immersive, or
                       $HOME/.local/share/illixr/boba_immersive.
  --source-dir DIR     Configure an existing companion checkout as it is.
                       Does not switch revisions or overwrite local edits.
  --repository URL     Use the SSH or HTTPS URL for ILLIXR/Boba-ILLIXR.
  --garden             Download and prepare the optional external Garden scene.
  --rebuild            Force the companion's CUDA extension rebuild.
  --dry-run            Print the source revision and paths without writing files.
  -h, --help           Show this help.

For two sibling repositories, run from ILLIXR:
  ./scripts/setup_boba_immersive.sh --install-root ..
  export BOBA_IMMERSIVE_ROOT="$(realpath ../Boba-ILLIXR)"

Install the native Quest APK separately with scripts/install_quest_app.sh --boba.
HELP
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --install-root|--source-dir|--repository)
            [[ $# -ge 2 && -n "$2" ]] || die "$1 requires a value."
            case "$1" in
                --install-root) INSTALL_ROOT="$2"; INSTALL_ROOT_EXPLICIT=1 ;;
                --source-dir) SOURCE_DIR="$2" ;;
                --repository) REPOSITORY="$2" ;;
            esac
            shift 2 ;;
        --garden|--rebuild) SETUP_ARGS+=("$1"); shift ;;
        --dry-run) DRY_RUN=1; shift ;;
        --skip-public-assets|--keep-downloads)
            log "$1 is obsolete: the default demo assets are packaged in Boba-ILLIXR."
            shift ;;
        -h|--help) usage; exit 0 ;;
        *) die "Unknown option: $1" ;;
    esac
done

[[ -z "${SOURCE_DIR}" || ${INSTALL_ROOT_EXPLICIT} -eq 0 ]] ||
    die '--source-dir and --install-root cannot be combined.'
case "${REPOSITORY%.git}" in
    git@github.com:ILLIXR/Boba-ILLIXR|https://github.com/ILLIXR/Boba-ILLIXR) ;;
    *) die 'The repository URL must identify ILLIXR/Boba-ILLIXR.' ;;
esac
if [[ -n "${SOURCE_DIR}" ]]; then
    BOBA_ROOT="$(realpath -m -- "${SOURCE_DIR}")"
    log "Existing source checkout: ${BOBA_ROOT} (revision and edits preserved)"
else
    INSTALL_ROOT="$(realpath -m -- "${INSTALL_ROOT}")"
    BOBA_ROOT="${INSTALL_ROOT}/Boba-ILLIXR"
    log "Pinned checkout: ${REPOSITORY} at ${BOBA_REF}"
    log "Destination: ${BOBA_ROOT}"
fi
log 'Conda environment: boba-cu132; runtime and default assets come from this repository.'
[[ ${DRY_RUN} -eq 0 ]] || exit 0

command -v git >/dev/null || die 'Git is required.'
git lfs version >/dev/null || die 'Git LFS is required to download the packaged Sloth asset.'

if [[ -z "${SOURCE_DIR}" ]]; then
    if [[ ! -e "${BOBA_ROOT}" ]]; then
        mkdir -p "${INSTALL_ROOT}"
        GIT_LFS_SKIP_SMUDGE=1 git clone --no-checkout "${REPOSITORY}" "${BOBA_ROOT}" ||
            die 'Clone failed. Verify your GitHub access and SSH key or HTTPS credential helper.'
    fi
    [[ -d "${BOBA_ROOT}/.git" || -f "${BOBA_ROOT}/.git" ]] || die "Not a Git checkout: ${BOBA_ROOT}"
    ORIGIN="$(git -C "${BOBA_ROOT}" remote get-url origin)"
    case "${ORIGIN%.git}" in
        git@github.com:ILLIXR/Boba-ILLIXR|https://github.com/ILLIXR/Boba-ILLIXR) ;;
        *) die "Existing checkout has a different origin: ${ORIGIN}" ;;
    esac
    # A --no-checkout clone has an empty index; only check edits once its files
    # have been materialized. Git itself also refuses to clobber untracked files.
    if [[ -e "${BOBA_ROOT}/boba_app.sh" ]] &&
       [[ -n "$(git -C "${BOBA_ROOT}" status --porcelain)" ]]; then
        die "Local edits at ${BOBA_ROOT}; use --source-dir to use them, or a separate --install-root."
    fi
    if ! git -C "${BOBA_ROOT}" cat-file -e "${BOBA_REF}^{commit}" 2>/dev/null; then
        GIT_LFS_SKIP_SMUDGE=1 git -C "${BOBA_ROOT}" fetch --depth 1 origin "${BOBA_REF}"
    fi
    GIT_LFS_SKIP_SMUDGE=1 git -C "${BOBA_ROOT}" checkout --detach "${BOBA_REF}"
else
    [[ -d "${BOBA_ROOT}/.git" || -f "${BOBA_ROOT}/.git" ]] || die "Not a Git checkout: ${BOBA_ROOT}"
    ACTUAL_REF="$(git -C "${BOBA_ROOT}" rev-parse HEAD)"
    if [[ "${ACTUAL_REF}" != "${BOBA_REF}" ]]; then
        log "Using source revision ${ACTUAL_REF}; ILLIXR pins ${BOBA_REF}."
    fi
fi

[[ -x "${BOBA_ROOT}/env_install/setup.sh" && -x "${BOBA_ROOT}/boba_app.sh" ]] ||
    die 'The checkout is missing the Boba-ILLIXR setup script or launcher.'
git -C "${BOBA_ROOT}" lfs install --local
git -C "${BOBA_ROOT}" lfs pull ||
    die 'Git LFS download failed. Verify access to ILLIXR/Boba-ILLIXR and its LFS objects.'
"${BOBA_ROOT}/env_install/setup.sh" "${SETUP_ARGS[@]}"
log 'Boba immersive setup is complete.'
log "Set BOBA_IMMERSIVE_ROOT=${BOBA_ROOT} when launching ILLIXR."
