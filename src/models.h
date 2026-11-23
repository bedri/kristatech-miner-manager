#pragma once

#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QtGlobal>

enum class MinerEngine {
    CpuMiner = 0,
    CcMiner = 1
};

enum class TargetType {
    LocalWallet = 0,
    PodmanContainer = 1,
    CustomRpc = 2,
    RemoteSsh = 3
};

enum class WorkerStatus {
    Stopped = 0,
    Starting = 1,
    Waiting = 2,
    Solving = 3,
    Adopted = 4,
    Error = 5,
    Restarting = 6
};

struct RpcCredentials {
    QString url;
    QString user;
    QString password;
    bool valid{false};
    QString error;
};

struct WorkerProfile {
    QString id;
    QString name;
    MinerEngine engine{MinerEngine::CpuMiner};
    QString binaryPath;
    TargetType targetType{TargetType::LocalWallet};

    // Connection details by target type
    QString confPath;          // LocalWallet
    QString podmanName;        // PodmanContainer
    QString rpcUrl;            // CustomRpc or RemoteSsh
    QString rpcUser;           // CustomRpc or RemoteSsh
    QString rpcPassword;       // CustomRpc or RemoteSsh (NEVER serialized for Local/Podman)
    QString sshTarget;         // RemoteSsh (user@host)
    int sshPort{22};           // RemoteSsh
    QString remoteBinaryPath;  // RemoteSsh

    // Miner settings
    QString coinbaseAddress;
    int threads{1};            // cpuminer -t
    QString cpuAffinity;       // taskset -c (e.g. "0" or "0,1")
    int maxTemp{0};            // cpuminer --max-temp (0 = disabled)
    QString gpuDevices;        // ccminer -d (e.g. "0")
    double gpuIntensity{0.0};  // ccminer -i (0 = auto)
    int apiPort{0};            // 0 = disabled
    QString extraArgs;

    bool autoRestart{true};
    bool disableInternalGenOnStart{false};
    bool enabled{true};

    /** Returns canonical node identifier shared by workers targeting the same daemon. */
    QString targetKey() const;
    QJsonObject toJson() const;
    static WorkerProfile fromJson(const QJsonObject& obj);
};

struct PoolSeatInfo {
    bool known{false};
    bool registered{false};
    QString track;
    qint64 unlockHeight{0};
    qint64 remainingBlocks{0};
    qint64 chainHeight{0};

    bool hasExpiry() const;
    qint64 renewalThresholdBlocks(const QString& chain = QString()) const;
    bool isExpiringSoon(const QString& chain = QString()) const;
    bool isRenewableTrack() const;
};

struct NodeMiningStatus {
    bool reachable{false};
    bool checked{false};
    qint64 blockHeight{0};
    int poolSize{0};
    bool internalGenerate{false};
    int internalThreads{0};
    double networkHashPs{0.0};
    double difficulty{0.0};
    QString chain;
    QString nodeErrors;
    bool walletChecked{false};
    bool walletEncrypted{false};
    bool walletLocked{false};
    qint64 walletUnlockedUntil{0};
    bool initialBlockDownload{false};
    QString error;
};

struct ConsensusStatus {
    bool known{false};
    QString chain;
    bool initialBlockDownload{false};
    bool fairnessActive{false};
    bool modelDActive{false};
    bool openEntryActive{false};
    bool pomblActive{false};
    QStringList activeUpgrades;

    QString badgeLabel() const;
};

struct GpuTelemetry {
    int index{0};
    QString name;
    int tempC{0};
    double powerW{0.0};
    double powerLimitW{0.0};
    int utilPct{0};
    int memUsedMB{0};
    int memTotalMB{0};
};

struct PodmanContainerInfo {
    QString name;
    bool running{false};
    qint64 pid{0};
    QString rpcUrl;
    QString rpcUser;
    QString rpcPassword;
    QString assignedAddress;
};

struct DiscoveredProcess {
    qint64 pid{0};
    qint64 wrapperPid{0};
    MinerEngine engine{MinerEngine::CpuMiner};
    QString binaryPath;
    TargetType targetType{TargetType::LocalWallet};
    QString podmanName;
    QString rpcUrl;
    QString coinbaseAddress;
    int threads{1};
    QString gpuDevices;
    double gpuIntensity{0.0};
    QString logPath;
};

struct WorkerRuntime {
    static constexpr int kMaxLogLines = 500;

    WorkerStatus status{WorkerStatus::Stopped};
    qint64 pid{0};
    qint64 wrapperPid{0};
    bool adopted{false};
    bool userStopped{true};

    double hashrateKhs{0.0};
    QString hashrateDisplay;
    int accepted{0};
    int stale{0};
    int rejected{0};
    qint64 lastBlock{0};
    int cpuTempC{-1};

    bool warningDeferred{false};
    QString warningDeferredMsg;
    bool warningWalletLocked{false};
    QString warningWalletLockedMsg;
    bool warningRpcError{false};
    QString warningRpcErrorMsg;
    QString lastError;

    int restartAttempts{0};
    qint64 nextRetryEpochMs{0};
    qint64 startedAtEpochMs{0};
    qint64 lastActiveEpochMs{0};

    QString logFilePath;
    qint64 logReadOffset{0};
    QStringList logLines;

    void appendLogLine(const QString& sanitizedLine);
    void resetRuntimeStats();
};

QString engineToString(MinerEngine engine);
MinerEngine engineFromString(const QString& str);
QString targetTypeToString(TargetType target);
TargetType targetTypeFromString(const QString& str);
QString statusToKey(WorkerStatus status);

bool isValidWorkerId(const QString& id);
bool isValidSshTarget(const QString& target);
QString normalizeRpcUrl(const QString& rawUrl);
bool isLoopbackRpcUrl(const QString& rawUrl);
QString sanitizeWorkerId(const QString& raw);
QString formatHashrateKhs(double khs);
QString formatSeatTrackBadge(const PoolSeatInfo& seat);

Q_DECLARE_METATYPE(WorkerProfile)
Q_DECLARE_METATYPE(PoolSeatInfo)
Q_DECLARE_METATYPE(NodeMiningStatus)
Q_DECLARE_METATYPE(ConsensusStatus)
Q_DECLARE_METATYPE(GpuTelemetry)
