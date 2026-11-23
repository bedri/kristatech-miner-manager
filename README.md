# KristaTech Miner Manager

Standalone C++17 / Qt6 desktop fleet manager for external `cpuminer` (CPU) and `ccminer` (CUDA GPU) workers on the KristaTech (`KRISTA` `v1.5.9.0` / `UPGRADE_FAIRNESS`) ADAM consensus network.

## Features

- **Multi-Engine Support:** Manages both `cpuminer` (`-a adam`, `-t`, `taskset -c`, `--max-temp`) and `ccminer` (`-a adam`, `-d`, `-i`) without shell wrappers.
- **4 Hardened Target Node Types:**
  - **Local Wallet** (reads `~/.kristatech/kristatech.conf` / `%APPDATA%\KristaTech\kristatech.conf` dynamically)
  - **Podman Containers** (`krista-cold-*` auto-discovery, automatic async `podman start` for stopped containers, `podman unshare nsenter -t <pid> -n`, and automatic stale `netns` detection & reattach on container restart)
  - **Custom / Remote RPC** (normalized `http://` / `https://` JSON-RPC with strict process adoption isolation)
  - **Remote SSH Host** (key-based SSH with `BatchMode=yes`, `--` argument separator, `nohup`/`setsid` + `~/.kristatech-mm/<id>.{pid,log,json}`, buffered `tail -F`, and periodic health/restart monitoring)
- **KRISTA `v1.5.9.0` / `UPGRADE_FAIRNESS` Protocol Alignment:**
  - Queries `getmininginfo`, `getadamminers`, `getwalletinfo`, and `getblockchaininfo` asynchronously
  - Displays active consensus phase (`Fairness`, `Model D`) and network chain (`Mainnet`, `Testnet`, `Regtest`), plus Dual-Track pool composition (`7+1+3`: `masternode`, `coin_lock`, `pow_lock`, `zerocoin_pow`)
  - Detects encrypted **Locked Wallets** (`unlocked_until == 0` and log warnings) across target nodes so ADAM puzzle signing or seat registrations are never silently skipped
  - Tracks **Early Seat Renewal Windows** (`<= 240` blocks for 250-KRISTA `coin_lock` & 10-KRISTA `pow_lock`, `<= 24` blocks for 0-KRISTA `zerocoin_pow`; short Testnet/Regtest thresholds) and provides confirmed one-click `registerminer` renewal from the Inspector banner
  - Flags unregistered or `ADAM Auto-Register deferred` workers (with optional **Auto-Stop Deferred** toggle) to prevent wasted hashpower
  - Bidirectional **Disable / Enable Internal Gen** (`setgenerate false/true`) to avoid internal + external double-mining
  - Live `nvidia-smi` GPU temperature, power, utilization, and VRAM telemetry
- **Process Lifecycle & Auto-Adopt:**
  - Automatically detects and adopts live `cpuminer`/`ccminer` processes via `/proc` (Linux) or Toolhelp/CIM (Windows)
  - Exponential-backoff auto-restart on unexpected crash
  - Minimizes to System Tray; Quit dialog asks whether to stop managed miners or detach them
- **Bilingual & Themed UI:** Live **English / Türkçe** switching and **Dark / Light** KristaTech palette (`#5576B2` Slate-Blue & `#F5897C` Salmon), with native/Qt file-dialog contrast styling and responsive Wayland table column fitting.

## Security

- `~/.config/KristaTechMinerManager` is created with `0700` permissions; `profiles.json`, temporary `-c` runtime configs, and worker logs use `0600`.
- Local wallet and Podman RPC passwords are read on demand from `.conf` files and are **never** serialized into `profiles.json` (or exported bundles).
- Credentials are passed to `cpuminer`/`ccminer` via `0600` JSON `-c` config files (never on CLI `--userpass`) and redacted in logs.
- All process execution uses direct argument arrays (`QProcess`) with strict worker ID (`^[a-zA-Z0-9_-]{1,64}$`) and SSH target validation, avoiding `sh -c` locally.

## Build, Test & Package

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

Run headless smoke test:

```bash
QT_QPA_PLATFORM=offscreen ./build/kristatech-miner-manager --smoke-test
```

Portable container builds (Linux Qt 6.2.4 AppDir/tarball & Windows MinGW Qt 6.8.3 zip):

```bash
./scripts/package_linux_in_container.sh
./scripts/package_windows_in_container.sh
```
