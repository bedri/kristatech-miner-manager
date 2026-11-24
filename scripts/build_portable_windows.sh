#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd)"
CONTAINER_ENGINE="${CONTAINER_ENGINE:-podman}"
IMAGE_NAME="${KMM_WIN64_BUILDER_IMAGE:-localhost/kmm-win64-builder:fedora41}"
CONTAINERFILE="${SCRIPT_DIR}/Containerfile.win64-builder"

REBUILD_IMAGE=0
IMAGE_ONLY=0

for arg in "$@"; do
    case "$arg" in
        --rebuild-image)
            REBUILD_IMAGE=1
            ;;
        --image-only)
            IMAGE_ONLY=1
            ;;
        -h|--help)
            cat <<EOF
Usage: $(basename "$0") [--rebuild-image] [--image-only]

Builds the portable Windows x86_64 package (.zip) using the cached
builder image (${IMAGE_NAME}) so dnf packages are only installed once.

  --rebuild-image   Rebuild the local builder image from scratch (--no-cache)
  --image-only      Ensure/build the builder image and exit without packaging
EOF
            exit 0
            ;;
        *)
            echo "Unknown option: $arg" >&2
            exit 2
            ;;
    esac
done

if [ "${REBUILD_IMAGE}" = "1" ]; then
    echo "Rebuilding Windows builder image ${IMAGE_NAME}..."
    "${CONTAINER_ENGINE}" build --no-cache -f "${CONTAINERFILE}" -t "${IMAGE_NAME}" "${SCRIPT_DIR}"
elif ! "${CONTAINER_ENGINE}" image exists "${IMAGE_NAME}"; then
    echo "Windows builder image ${IMAGE_NAME} not found locally; building once..."
    "${CONTAINER_ENGINE}" build -f "${CONTAINERFILE}" -t "${IMAGE_NAME}" "${SCRIPT_DIR}"
fi

if [ "${IMAGE_ONLY}" = "1" ]; then
    echo "Windows builder image ready: ${IMAGE_NAME}"
    exit 0
fi

exec "${CONTAINER_ENGINE}" run --rm \
    -v "${REPO_ROOT}:/work:Z" \
    -w /work \
    "${IMAGE_NAME}" \
    bash /work/scripts/package_windows_in_container.sh
