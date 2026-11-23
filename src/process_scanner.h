#pragma once

#include "models.h"

#include <QObject>
#include <QVector>

#include <functional>

/**
 * Discovers live cpuminer/ccminer processes via /proc, Podman containers,
 * local binaries, and NVIDIA CUDA GPUs via nvidia-smi.
 */
class ProcessScanner : public QObject
{
    Q_OBJECT

public:
    explicit ProcessScanner(QObject* parent = nullptr);
    ~ProcessScanner() override;

    static QString defaultCpuMinerPath();
    static QString defaultCcMinerPath();

    /** Parses CLI arguments from /proc/<pid>/cmdline or wrapper strings (pure helper, unit-tested). */
    static bool parseCmdline(const QStringList& args, DiscoveredProcess& out);

    /** Scans /proc for running cpuminer/ccminer processes and maps Podman namespaces + stdout logs. */
    static QVector<DiscoveredProcess> scanRunningMiners();

    /** Discovers local krista-cold-* Podman containers and their configs/addresses. */
    static QVector<PodmanContainerInfo> discoverPodmanContainers();

    /** Checks whether a process PID is currently alive (signal 0). */
    static bool isPidAlive(qint64 pid);

    /** Verifies whether PID still belongs to a miner or wrapper binary (guards against PID reuse before SIGKILL). */
    static bool isManagedMinerOrWrapperPid(qint64 pid);

    /** Finds child PID of wrapper (e.g., podman unshare nsenter -> cpuminer). */
    static qint64 findChildMinerPid(qint64 parentPid);

    /** Reads /proc/<pid>/ns/net symlink target (e.g. "net:[4026535289]") on Linux; empty on Windows or error. */
    static QString readProcessNetNs(qint64 pid);

    /** Queries nvidia-smi asynchronously for CUDA GPU telemetry. */
    void queryNvidiaGpusAsync(std::function<void(const QVector<GpuTelemetry>& gpus)> cb);
};
