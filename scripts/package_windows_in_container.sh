#!/usr/bin/env bash
set -euo pipefail

WORK_DIR="${WORK_DIR:-/work}"
BUILD_DIR="${WORK_DIR}/build-win64-portable"
STAGE_NAME="KristaTechMinerManager-windows-x86_64"
DIST_DIR="${WORK_DIR}/dist"
STAGE_DIR="${DIST_DIR}/${STAGE_NAME}"
ZIP_OUT="${DIST_DIR}/${STAGE_NAME}.zip"

win64_deps_ready() {
    command -v mingw64-cmake >/dev/null 2>&1 \
        && command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1 \
        && command -v x86_64-w64-mingw32-objdump >/dev/null 2>&1 \
        && command -v ninja >/dev/null 2>&1 \
        && command -v zip >/dev/null 2>&1 \
        && command -v file >/dev/null 2>&1 \
        && command -v python3 >/dev/null 2>&1 \
        && [ -f /usr/x86_64-w64-mingw32/sys-root/mingw/lib/cmake/Qt6/Qt6Config.cmake ] \
        && [ -f /usr/lib64/cmake/Qt6/Qt6Config.cmake ]
}

if [ "${KMM_SKIP_DEPS:-0}" = "1" ] && win64_deps_ready; then
    echo "Using pre-installed Windows MinGW build dependencies (KMM_SKIP_DEPS=1)."
elif [ "${KMM_FORCE_DEPS:-0}" != "1" ] && win64_deps_ready; then
    echo "Windows MinGW build dependencies already present; skipping dnf install."
else
    dnf install -y \
        --setopt=install_weak_deps=False \
        --setopt=fastestmirror=True \
        --setopt=max_parallel_downloads=10 \
        --setopt=retries=10 \
        --setopt=timeout=20 \
        --setopt=minrate=20000 \
        mingw64-gcc-c++ \
        mingw64-cmake \
        mingw64-qt6-qtbase \
        mingw64-binutils \
        qt6-qtbase-devel \
        ninja-build \
        zip \
        file \
        python3
fi

rm -rf "${BUILD_DIR}" "${STAGE_DIR}" "${ZIP_OUT}"
mkdir -p "${DIST_DIR}"

mingw64-cmake -S "${WORK_DIR}" -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DQT_HOST_PATH=/usr \
    -DKMM_BUILD_TESTS=OFF
cmake --build "${BUILD_DIR}" -j"$(nproc)"

mkdir -p "${STAGE_DIR}"
cp -a "${BUILD_DIR}/kristatech-miner-manager.exe" "${STAGE_DIR}/kristatech-miner-manager.exe"

MINGW_ROOT="/usr/x86_64-w64-mingw32/sys-root/mingw"
MINGW_BIN="${MINGW_ROOT}/bin"

QT_PLUGINS_DIR=""
for cand in \
    "${MINGW_ROOT}/lib/qt6/plugins" \
    "${MINGW_ROOT}/share/qt6/plugins" \
    "${MINGW_ROOT}/plugins"; do
    if [ -d "$cand" ]; then
        QT_PLUGINS_DIR="$cand"
        break
    fi
done
if [ -z "${QT_PLUGINS_DIR}" ]; then
    echo "ERROR: Could not locate MinGW Qt6 plugins directory" >&2
    exit 1
fi

for sub in platforms styles imageformats tls networkinformation; do
    if [ -d "${QT_PLUGINS_DIR}/${sub}" ]; then
        mkdir -p "${STAGE_DIR}/plugins/${sub}"
        find "${QT_PLUGINS_DIR}/${sub}" -maxdepth 1 -name '*.dll' ! -name '*d.dll' -exec cp -L {} "${STAGE_DIR}/plugins/${sub}/" \;
        # Ensure we did not accidentally filter out a real plugin ending in d (like qminimald vs non-debug); copy if empty
        if [ -z "$(ls -A "${STAGE_DIR}/plugins/${sub}" 2>/dev/null)" ]; then
            find "${QT_PLUGINS_DIR}/${sub}" -maxdepth 1 -name '*.dll' -exec cp -L {} "${STAGE_DIR}/plugins/${sub}/" \;
        fi
    fi
done

cat > "${STAGE_DIR}/qt.conf" <<'EOF'
[Paths]
Plugins = plugins
EOF

python3 - "${STAGE_DIR}" "${MINGW_BIN}" <<'PYEOF'
import os
import re
import shutil
import subprocess
import sys

stage_dir = sys.argv[1]
mingw_bin = sys.argv[2]

WINDOWS_SYSTEM_DLLS = {
    "advapi32.dll", "authz.dll", "bcrypt.dll", "cfgmgr32.dll", "comctl32.dll",
    "comdlg32.dll", "crypt32.dll", "d3d11.dll", "d3d12.dll", "d3d9.dll",
    "dcomp.dll", "dnsapi.dll", "dwmapi.dll", "dwrite.dll", "dxgi.dll",
    "gdi32.dll", "gdiplus.dll", "imm32.dll", "iphlpapi.dll", "kernel32.dll",
    "kernelbase.dll", "mpr.dll", "msvcp140.dll", "msvcrt.dll", "ncrypt.dll",
    "netapi32.dll", "ntdll.dll", "ole32.dll", "oleaut32.dll", "powrprof.dll",
    "psapi.dll", "rpcrt4.dll", "sechost.dll", "secur32.dll", "setupapi.dll",
    "shcore.dll", "shell32.dll", "shlwapi.dll", "ucrtbase.dll", "uiautomationcore.dll",
    "user32.dll", "userenv.dll", "uxtheme.dll", "vcruntime140.dll", "version.dll",
    "win32u.dll", "winhttp.dll", "winmm.dll", "ws2_32.dll", "wtsapi32.dll",
    "opengl32.dll", "glu32.dll", "cryptbase.dll", "wintrust.dll", "usp10.dll",
    "msimg32.dll", "normaliz.dll", "prntvpt.dll", "winspool.drv", "spoolss.dll",
}

def is_system_dll(name_lower: str) -> bool:
    return (
        name_lower in WINDOWS_SYSTEM_DLLS
        or name_lower.startswith("api-ms-win-")
        or name_lower.startswith("ext-ms-win-")
    )

# Case-insensitive index of available MinGW DLLs
mingw_map = {}
for fname in os.listdir(mingw_bin):
    if fname.lower().endswith(".dll"):
        mingw_map[fname.lower()] = os.path.join(mingw_bin, fname)

def pe_imports(pe_path: str):
    env = os.environ.copy()
    env["LC_ALL"] = "C"
    out = subprocess.check_output(["x86_64-w64-mingw32-objdump", "-p", pe_path], env=env, text=True)
    imports = []
    for line in out.splitlines():
        m = re.search(r"DLL Name:\s+(\S+)", line)
        if m:
            imports.append(m.group(1))
    return imports

queue = [os.path.join(stage_dir, "kristatech-miner-manager.exe")]
for root, _, files in os.walk(os.path.join(stage_dir, "plugins")):
    for f in files:
        if f.lower().endswith(".dll"):
            queue.append(os.path.join(root, f))

# Include OpenSSL DLLs if qopensslbackend.dll exists in tls plugin folder (sometimes runtime-loaded)
if os.path.exists(os.path.join(stage_dir, "plugins", "tls", "qopensslbackend.dll")):
    for lower_name, full_path in mingw_map.items():
        if lower_name.startswith("libssl-") or lower_name.startswith("libcrypto-"):
            dst = os.path.join(stage_dir, os.path.basename(full_path))
            if not os.path.exists(dst):
                shutil.copy2(full_path, dst)
                queue.append(dst)

visited = set()
copied_names = set()
for fname in os.listdir(stage_dir):
    if fname.lower().endswith(".dll"):
        copied_names.add(fname.lower())

while queue:
    cur = queue.pop(0)
    if cur in visited:
        continue
    visited.add(cur)
    for imp in pe_imports(cur):
        imp_low = imp.lower()
        if imp_low in copied_names:
            continue
        if imp_low in mingw_map:
            src = mingw_map[imp_low]
            dst = os.path.join(stage_dir, os.path.basename(src))
            shutil.copy2(src, dst)
            copied_names.add(imp_low)
            queue.append(dst)
        elif not is_system_dll(imp_low):
            raise RuntimeError(f"Unresolved non-system PE dependency in {cur}: {imp}")

print(f"Verified and bundled {len(copied_names)} MinGW/Qt6 DLLs into {stage_dir}")
PYEOF

file "${STAGE_DIR}/kristatech-miner-manager.exe"

cd "${DIST_DIR}"
zip -r -9 "${ZIP_OUT}" "${STAGE_NAME}" >/dev/null
rm -rf "${BUILD_DIR}"
ls -lh "${ZIP_OUT}"
