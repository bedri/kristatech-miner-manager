#!/usr/bin/env bash
set -euo pipefail

WORK_DIR="${WORK_DIR:-/work}"
BUILD_DIR="${WORK_DIR}/build-linux-portable"
STAGE_NAME="KristaTechMinerManager-linux-x86_64"
DIST_DIR="${WORK_DIR}/dist"
STAGE_DIR="${DIST_DIR}/${STAGE_NAME}"
TAR_OUT="${DIST_DIR}/${STAGE_NAME}.tar.gz"
RUN_OUT="${DIST_DIR}/${STAGE_NAME}.run"

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    qt6-base-dev \
    qt6-base-dev-tools \
    libgl-dev \
    libegl-dev \
    libxkbcommon-dev \
    patchelf \
    file \
    binutils \
    tar \
    gzip \
    ca-certificates \
    python3

for opt_pkg in qt6-wayland qt6-image-formats-plugins libqt6svg6; do
    apt-get install -y --no-install-recommends "$opt_pkg" || true
done

rm -rf "${BUILD_DIR}" "${STAGE_DIR}"
mkdir -p "${DIST_DIR}"

cmake -S "${WORK_DIR}" -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DKMM_BUILD_TESTS=ON
cmake --build "${BUILD_DIR}" -j"$(nproc)"
QT_QPA_PLATFORM=offscreen ctest --test-dir "${BUILD_DIR}" --output-on-failure

mkdir -p "${STAGE_DIR}/bin" "${STAGE_DIR}/lib" "${STAGE_DIR}/plugins"
cp -a "${BUILD_DIR}/kristatech-miner-manager" "${STAGE_DIR}/bin/kristatech-miner-manager"
strip --strip-unneeded "${STAGE_DIR}/bin/kristatech-miner-manager"

cat > "${STAGE_DIR}/bin/qt.conf" <<'EOF'
[Paths]
Prefix = ..
Plugins = plugins
Libraries = lib
EOF

QT_PLUGINS_DIR=""
for cand in \
    /usr/lib/x86_64-linux-gnu/qt6/plugins \
    /usr/lib/qt6/plugins; do
    if [ -d "$cand" ]; then
        QT_PLUGINS_DIR="$cand"
        break
    fi
done
if [ -z "${QT_PLUGINS_DIR}" ]; then
    echo "ERROR: Could not locate Qt6 plugins directory" >&2
    exit 1
fi

for sub in platforms styles imageformats tls networkinformation xcbglintegrations iconengines; do
    if [ -d "${QT_PLUGINS_DIR}/${sub}" ]; then
        mkdir -p "${STAGE_DIR}/plugins/${sub}"
        find "${QT_PLUGINS_DIR}/${sub}" -maxdepth 1 -name '*.so' -exec cp -L {} "${STAGE_DIR}/plugins/${sub}/" \;
    fi
done

if [ -d "${STAGE_DIR}/plugins/platforms" ]; then
    # Keep xcb + wayland (if present) + offscreen + minimal; skip vnc/eglfs/linuxfb if not needed
    for extra_wayland in wayland-shell-integration wayland-graphics-integration-client; do
        if [ -d "${QT_PLUGINS_DIR}/${extra_wayland}" ]; then
            mkdir -p "${STAGE_DIR}/plugins/${extra_wayland}"
            find "${QT_PLUGINS_DIR}/${extra_wayland}" -maxdepth 1 -name '*.so' -exec cp -L {} "${STAGE_DIR}/plugins/${extra_wayland}/" \;
        fi
    done
fi

python3 - "${STAGE_DIR}" <<'PYEOF'
import os
import re
import shutil
import subprocess
import sys

stage_dir = sys.argv[1]
lib_dir = os.path.join(stage_dir, "lib")
bin_path = os.path.join(stage_dir, "bin", "kristatech-miner-manager")
plugins_dir = os.path.join(stage_dir, "plugins")

EXCLUDE_PREFIXES = (
    "libc.so.",
    "libm.so.",
    "libdl.so.",
    "libpthread.so.",
    "librt.so.",
    "libresolv.so.",
    "libutil.so.",
    "ld-linux",
    "libnss_",
    "libstdc++.so.",
    "libgcc_s.so.",
    "libGL.so.",
    "libEGL.so.",
    "libGLX.so.",
    "libGLdispatch.so.",
    "libOpenGL.so.",
    "libGLESv2.so.",
    "libdrm.so.",
    "libgbm.so.",
    "libvulkan.so.",
    "libX11.so.",
    "libX11-xcb.so.",
    "libxcb.so.1",
    "libwayland-client.so.",
    "libwayland-egl.so.",
    "libwayland-server.so.",
    "libasound.so.",
    "libpulse",
    "libpipewire",
)

def is_excluded(soname: str) -> bool:
    return any(soname.startswith(p) for p in EXCLUDE_PREFIXES)

def scan_ldd(elf_path: str):
    env = os.environ.copy()
    env["LC_ALL"] = "C"
    out = subprocess.check_output(["ldd", elf_path], env=env, text=True)
    deps = []
    for line in out.splitlines():
        line = line.strip()
        if not line:
            continue
        if "not found" in line:
            raise RuntimeError(f"Unresolved shared library in {elf_path}: {line}")
        m = re.match(r"^(\S+)\s+=>\s+(/\S+)\s+\(", line)
        if m:
            deps.append((m.group(1), m.group(2)))
    return deps

roots = [bin_path]
for root, _, files in os.walk(plugins_dir):
    for f in files:
        if f.endswith(".so"):
            roots.append(os.path.join(root, f))

# Also ensure OpenSSL runtime libs if libqopensslbackend.so exists (dlopen fallback)
extra_openers = []
for candidate in (
    "/lib/x86_64-linux-gnu/libssl.so.3",
    "/lib/x86_64-linux-gnu/libcrypto.so.3",
    "/usr/lib/x86_64-linux-gnu/libssl.so.3",
    "/usr/lib/x86_64-linux-gnu/libcrypto.so.3",
):
    if os.path.exists(candidate):
        extra_openers.append((os.path.basename(candidate), candidate))

copied = {}
queue = list(roots)

def enqueue_dep(soname: str, resolved_path: str):
    if is_excluded(soname):
        return
    if soname in copied:
        return
    dst = os.path.join(lib_dir, soname)
    shutil.copy2(resolved_path, dst)
    os.chmod(dst, 0o755)
    copied[soname] = dst
    queue.append(dst)

while queue:
    cur = queue.pop(0)
    for soname, resolved in scan_ldd(cur):
        enqueue_dep(soname, resolved)

for soname, resolved in extra_openers:
    enqueue_dep(soname, resolved)

while queue:
    cur = queue.pop(0)
    for soname, resolved in scan_ldd(cur):
        enqueue_dep(soname, resolved)

print(f"Bundled {len(copied)} shared libraries into {lib_dir}")
PYEOF

patchelf --force-rpath --set-rpath '$ORIGIN/../lib' "${STAGE_DIR}/bin/kristatech-miner-manager"
for sofile in "${STAGE_DIR}/lib/"*.so*; do
    [ -e "$sofile" ] || continue
    patchelf --force-rpath --set-rpath '$ORIGIN' "$sofile"
done
while IFS= read -r -d '' pso; do
    patchelf --force-rpath --set-rpath '$ORIGIN/../../lib' "$pso"
done < <(find "${STAGE_DIR}/plugins" -type f -name '*.so' -print0)

cat > "${STAGE_DIR}/KristaTechMinerManager" <<'EOF'
#!/bin/sh
set -eu
HERE="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
export QT_PLUGIN_PATH="${HERE}/plugins"
unset QT_QPA_PLATFORM_PLUGIN_PATH
exec "${HERE}/bin/kristatech-miner-manager" "$@"
EOF
chmod +x "${STAGE_DIR}/KristaTechMinerManager"

cp -a "${WORK_DIR}/resources/kristatech-miner-manager.desktop" "${STAGE_DIR}/"
cp -a "${WORK_DIR}/resources/icons/kristatech-miner-manager_256.png" "${STAGE_DIR}/kristatech-miner-manager.png"

# Verify self-contained resolution inside container before archiving
ldd "${STAGE_DIR}/bin/kristatech-miner-manager" | grep "libQt6"
QT_QPA_PLATFORM=offscreen "${STAGE_DIR}/KristaTechMinerManager" --smoke-test

rm -f "${TAR_OUT}" "${RUN_OUT}"
tar -czf "${TAR_OUT}" -C "${DIST_DIR}" "${STAGE_NAME}"

PAYLOAD_SHA256="$(sha256sum "${TAR_OUT}" | awk '{print $1}')"
cat > "${RUN_OUT}" <<EOF
#!/bin/sh
set -eu
umask 077
PAYLOAD_HASH="${PAYLOAD_SHA256}"
CACHE_BASE=""
CLEANUP_BASE=""
if [ -n "\${XDG_CACHE_HOME:-}" ]; then
    CACHE_BASE="\${XDG_CACHE_HOME}/kristatech-miner-manager-portable"
elif [ -n "\${HOME:-}" ] && [ "\${HOME}" != "/" ]; then
    CACHE_BASE="\${HOME}/.cache/kristatech-miner-manager-portable"
else
    CLEANUP_BASE="\$(mktemp -d "\${TMPDIR:-/tmp}/kmm-run-XXXXXX")"
    chmod 700 "\${CLEANUP_BASE}"
    CACHE_BASE="\${CLEANUP_BASE}"
    trap 'rm -rf "\${CLEANUP_BASE}"' EXIT HUP INT TERM
fi
if [ -L "\${CACHE_BASE}" ]; then
    echo "ERROR: Refusing symlink cache directory: \${CACHE_BASE}" >&2
    exit 1
fi
mkdir -p "\${CACHE_BASE}"
chmod 700 "\${CACHE_BASE}"
TARGET_DIR="\${CACHE_BASE}/\${PAYLOAD_HASH}"
if [ -L "\${TARGET_DIR}" ]; then
    echo "ERROR: Refusing symlink target directory: \${TARGET_DIR}" >&2
    exit 1
fi
if [ ! -x "\${TARGET_DIR}/bin/kristatech-miner-manager" ]; then
    ARCHIVE_LINE=\$(awk '/^__ARCHIVE_BELOW__\$/{print NR + 1; exit 0}' "\$0")
    if command -v sha256sum >/dev/null 2>&1; then
        ACTUAL_HASH=\$(tail -n +"\${ARCHIVE_LINE}" "\$0" | sha256sum | awk '{print \$1}')
        if [ "\${ACTUAL_HASH}" != "\${PAYLOAD_HASH}" ]; then
            echo "ERROR: Integrity check failed for portable archive." >&2
            exit 1
        fi
    fi
    TMP_DIR="\$(mktemp -d "\${CACHE_BASE}/.tmp.\${PAYLOAD_HASH}.XXXXXX")"
    tail -n +"\${ARCHIVE_LINE}" "\$0" | tar -xz -C "\${TMP_DIR}" --strip-components=1
    if [ -d "\${TARGET_DIR}" ]; then
        rm -rf "\${TMP_DIR}"
    else
        mv "\${TMP_DIR}" "\${TARGET_DIR}" 2>/dev/null || rm -rf "\${TMP_DIR}"
    fi
fi
if [ -n "\${CLEANUP_BASE}" ]; then
    "\${TARGET_DIR}/KristaTechMinerManager" "\$@"
else
    exec "\${TARGET_DIR}/KristaTechMinerManager" "\$@"
fi
__ARCHIVE_BELOW__
EOF
cat "${TAR_OUT}" >> "${RUN_OUT}"
chmod +x "${RUN_OUT}"

QT_QPA_PLATFORM=offscreen "${RUN_OUT}" --smoke-test
rm -rf "${BUILD_DIR}"
ls -lh "${TAR_OUT}" "${RUN_OUT}"
