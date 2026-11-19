# KristaTech Miner Manager

Standalone C++17 / Qt6 desktop fleet manager for external `cpuminer` (CPU) and `ccminer` (CUDA GPU) workers on the KristaTech (`KRISTA`) ADAM consensus network.

## Features

- **Multi-Engine Support:** Manages both `cpuminer` (`-a adam`, `-t`, `taskset -c`, `--max-temp`) and `ccminer` (`-a adam`, `-d`, `-i`) without shell wrappers.
- **4 Target Node Types:**
  - **Local Wallet** (reads `~/.kristatech/kristatech.conf` dynamically)
  - **Podman Containers** (`krista-cold-*` auto-discovery + `podman unshare nsenter -t <pid> -n`)
  - **Custom / Remote RPC**
  - **Remote SSH Host** (key-based SSH with `nohup`/`setsid` + `~/.kristatech-mm/<id>.{pid,log,json}`)
- **ADAM Pool Awareness & Safeguards:**
  - Queries `getblockcount`, `getadamminers`, and `getmininginfo` asynchronously
  - Flags unregistered or `ADAM Auto-Register deferred` workers to prevent wasted hashpower
  - One-click or automatic `setgenerate false` to prevent internal + external double-mining
  - Live `nvidia-smi` GPU temperature, power, utilization, and VRAM telemetry
- **Process Lifecycle & Auto-Adopt:**
  - Automatically detects and adopts live `cpuminer`/`ccminer` processes via `/proc` on startup
  - Exponential-backoff auto-restart on unexpected crash
  - Minimizes to System Tray; Quit dialog asks whether to stop managed miners or detach them
- **Bilingual & Themed UI:** Live **English / Türkçe** switching and **Dark / Light** KristaTech palette (`#5576B2` Slate-Blue & `#F5897C` Salmon).

## Security

- `~/.config/KristaTechMinerManager` is created with `0700` permissions; `profiles.json`, temporary `-c` runtime configs, and worker logs use `0600`.
- Local wallet and Podman RPC passwords are read on demand from `.conf` files and are **never** serialized into `profiles.json`.
- Credentials are passed to `cpuminer`/`ccminer` via `0600` JSON `-c` config files (never on CLI `--userpass`) and redacted in logs.
- All process execution uses direct argument arrays (`QProcess`) with strict worker ID validation (`^[a-zA-Z0-9_-]+$`), avoiding `sh -c`.

## Build & Test

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

Run headless smoke test:

```bash
QT_QPA_PLATFORM=offscreen ./build/kristatech-miner-manager --smoke-test
```
