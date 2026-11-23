#include "models.h"

#include <QRegularExpression>
#include <QUrl>

QString engineToString(MinerEngine engine)
{
    return (engine == MinerEngine::CcMiner) ? QStringLiteral("ccminer") : QStringLiteral("cpuminer");
}

MinerEngine engineFromString(const QString& str)
{
    return (str.trimmed().toLower() == QStringLiteral("ccminer")) ? MinerEngine::CcMiner : MinerEngine::CpuMiner;
}

QString targetTypeToString(TargetType target)
{
    switch (target) {
    case TargetType::LocalWallet:
        return QStringLiteral("local");
    case TargetType::PodmanContainer:
        return QStringLiteral("podman");
    case TargetType::CustomRpc:
        return QStringLiteral("custom_rpc");
    case TargetType::RemoteSsh:
        return QStringLiteral("ssh");
    }
    return QStringLiteral("local");
}

TargetType targetTypeFromString(const QString& str)
{
    const QString s = str.trimmed().toLower();
    if (s == QStringLiteral("podman")) return TargetType::PodmanContainer;
    if (s == QStringLiteral("custom_rpc") || s == QStringLiteral("custom")) return TargetType::CustomRpc;
    if (s == QStringLiteral("ssh")) return TargetType::RemoteSsh;
    return TargetType::LocalWallet;
}

QString statusToKey(WorkerStatus status)
{
    switch (status) {
    case WorkerStatus::Stopped:
        return QStringLiteral("status.stopped");
    case WorkerStatus::Starting:
        return QStringLiteral("status.starting");
    case WorkerStatus::Waiting:
        return QStringLiteral("status.waiting");
    case WorkerStatus::Solving:
        return QStringLiteral("status.solving");
    case WorkerStatus::Adopted:
        return QStringLiteral("status.adopted");
    case WorkerStatus::Error:
        return QStringLiteral("status.error");
    case WorkerStatus::Restarting:
        return QStringLiteral("status.restarting");
    }
    return QStringLiteral("status.stopped");
}

bool isValidWorkerId(const QString& id)
{
    static const QRegularExpression re(QStringLiteral("^[a-zA-Z0-9_-]{1,64}$"));
    return re.match(id).hasMatch();
}

bool isValidSshTarget(const QString& target)
{
    const QString t = target.trimmed();
    if (t.isEmpty() || t.startsWith(QLatin1Char('-'))) {
        return false;
    }
    static const QRegularExpression re(
        QStringLiteral("^(?:[a-zA-Z0-9._%+-]+@)?[a-zA-Z0-9\\[][a-zA-Z0-9._:\\[\\]-]{0,254}$"));
    return re.match(t).hasMatch();
}

QString normalizeRpcUrl(const QString& rawUrl)
{
    QString s = rawUrl.trimmed();
    if (s.isEmpty()) {
        return {};
    }
    for (const QChar ch : s) {
        if (ch.isSpace() || ch.unicode() < 32) {
            return {};
        }
    }
    if (!s.contains(QStringLiteral("://"))) {
        s.prepend(QStringLiteral("http://"));
    }
    const QUrl u(s, QUrl::StrictMode);
    const QString scheme = u.scheme().toLower();
    if (!u.isValid() || (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) || u.host().isEmpty()) {
        return {};
    }
    QUrl norm = u;
    norm.setScheme(scheme);
    norm.setHost(u.host().toLower());
    if (norm.path().isEmpty()) {
        norm.setPath(QStringLiteral("/"));
    }
    return norm.toString();
}

bool isLoopbackRpcUrl(const QString& rawUrl)
{
    const QString norm = normalizeRpcUrl(rawUrl);
    if (norm.isEmpty()) {
        return false;
    }
    const QString host = QUrl(norm).host().toLower();
    return host == QStringLiteral("127.0.0.1")
        || host == QStringLiteral("localhost")
        || host == QStringLiteral("::1")
        || host == QStringLiteral("0.0.0.0");
}

QString sanitizeWorkerId(const QString& raw)
{
    QString out;
    out.reserve(raw.size());
    for (QChar ch : raw) {
        if (ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('-')) {
            out.append(ch);
        } else {
            out.append(QLatin1Char('_'));
        }
    }
    while (out.contains(QStringLiteral("__"))) {
        out.replace(QStringLiteral("__"), QStringLiteral("_"));
    }
    if (out.size() > 64) {
        out.truncate(64);
    }
    if (out.isEmpty()) {
        out = QStringLiteral("worker");
    }
    return out;
}

bool PoolSeatInfo::hasExpiry() const
{
    return registered && unlockHeight > 0;
}

qint64 PoolSeatInfo::renewalThresholdBlocks(const QString& chain) const
{
    if (!hasExpiry()) {
        return 0;
    }
    const QString c = chain.trimmed().toLower();
    const bool isShortTest = (c == QStringLiteral("test") || c == QStringLiteral("regtest"));
    if (track == QStringLiteral("zerocoin_pow")) {
        return isShortTest ? 8 : 24;
    }
    return isShortTest ? 15 : 240;
}

bool PoolSeatInfo::isExpiringSoon(const QString& chain) const
{
    return hasExpiry() && remainingBlocks <= renewalThresholdBlocks(chain);
}

bool PoolSeatInfo::isRenewableTrack() const
{
    return track == QStringLiteral("coin_lock")
        || track == QStringLiteral("pow_lock")
        || track == QStringLiteral("zerocoin_pow");
}

QString formatSeatTrackBadge(const PoolSeatInfo& seat)
{
    if (!seat.registered) {
        return {};
    }
    if (!seat.hasExpiry()) {
        return seat.track;
    }
    return QStringLiteral("%1 (%2b)").arg(seat.track).arg(seat.remainingBlocks);
}

QString ConsensusStatus::badgeLabel() const
{
    if (fairnessActive) {
        return QStringLiteral("Fairness");
    }
    if (modelDActive) {
        return QStringLiteral("Model D");
    }
    return {};
}

QString formatHashrateKhs(double khs)
{
    if (khs <= 0.0) {
        return QStringLiteral("—");
    }
    if (khs >= 1000000.0) {
        return QString::number(khs / 1000000.0, 'f', 2) + QStringLiteral(" GH/s");
    }
    if (khs >= 1000.0) {
        return QString::number(khs / 1000.0, 'f', 2) + QStringLiteral(" MH/s");
    }
    if (khs >= 1.0) {
        return QString::number(khs, 'f', 2) + QStringLiteral(" kH/s");
    }
    return QString::number(khs * 1000.0, 'f', 1) + QStringLiteral(" H/s");
}

QString WorkerProfile::targetKey() const
{
    switch (targetType) {
    case TargetType::LocalWallet:
        return QStringLiteral("local:") + confPath.trimmed();
    case TargetType::PodmanContainer:
        return QStringLiteral("podman:") + podmanName.trimmed();
    case TargetType::CustomRpc:
        return QStringLiteral("rpc:") + rpcUrl.trimmed();
    case TargetType::RemoteSsh:
        return QStringLiteral("ssh:") + sshTarget.trimmed() + QLatin1Char(':') + rpcUrl.trimmed();
    }
    return QStringLiteral("local");
}

QJsonObject WorkerProfile::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("name"), name);
    obj.insert(QStringLiteral("engine"), engineToString(engine));
    obj.insert(QStringLiteral("binaryPath"), binaryPath);
    obj.insert(QStringLiteral("targetType"), targetTypeToString(targetType));

    if (!confPath.isEmpty()) {
        obj.insert(QStringLiteral("confPath"), confPath);
    }
    if (!podmanName.isEmpty()) {
        obj.insert(QStringLiteral("podmanName"), podmanName);
    }
    if (!rpcUrl.isEmpty()) {
        obj.insert(QStringLiteral("rpcUrl"), rpcUrl);
    }
    // Security invariant: LocalWallet and PodmanContainer read credentials live from .conf
    if (targetType == TargetType::CustomRpc || targetType == TargetType::RemoteSsh) {
        if (!rpcUser.isEmpty()) {
            obj.insert(QStringLiteral("rpcUser"), rpcUser);
        }
        if (!rpcPassword.isEmpty()) {
            obj.insert(QStringLiteral("rpcPassword"), rpcPassword);
        }
    }
    if (targetType == TargetType::RemoteSsh) {
        obj.insert(QStringLiteral("sshTarget"), sshTarget);
        obj.insert(QStringLiteral("sshPort"), sshPort);
        obj.insert(QStringLiteral("remoteBinaryPath"), remoteBinaryPath);
    }

    obj.insert(QStringLiteral("coinbaseAddress"), coinbaseAddress);
    obj.insert(QStringLiteral("threads"), threads);
    if (!cpuAffinity.isEmpty()) {
        obj.insert(QStringLiteral("cpuAffinity"), cpuAffinity);
    }
    if (maxTemp > 0) {
        obj.insert(QStringLiteral("maxTemp"), maxTemp);
    }
    if (!gpuDevices.isEmpty()) {
        obj.insert(QStringLiteral("gpuDevices"), gpuDevices);
    }
    if (gpuIntensity > 0.0) {
        obj.insert(QStringLiteral("gpuIntensity"), gpuIntensity);
    }
    if (apiPort > 0) {
        obj.insert(QStringLiteral("apiPort"), apiPort);
    }
    if (!extraArgs.isEmpty()) {
        obj.insert(QStringLiteral("extraArgs"), extraArgs);
    }
    obj.insert(QStringLiteral("autoRestart"), autoRestart);
    obj.insert(QStringLiteral("disableInternalGenOnStart"), disableInternalGenOnStart);
    obj.insert(QStringLiteral("enabled"), enabled);
    return obj;
}

WorkerProfile WorkerProfile::fromJson(const QJsonObject& obj)
{
    WorkerProfile p;
    p.name = obj.value(QStringLiteral("name")).toString();
    const QString rawId = obj.value(QStringLiteral("id")).toString();
    p.id = isValidWorkerId(rawId) ? rawId : sanitizeWorkerId(rawId.isEmpty() ? p.name : rawId);
    p.engine = engineFromString(obj.value(QStringLiteral("engine")).toString());
    p.binaryPath = obj.value(QStringLiteral("binaryPath")).toString();
    p.targetType = targetTypeFromString(obj.value(QStringLiteral("targetType")).toString());

    p.confPath = obj.value(QStringLiteral("confPath")).toString();
    p.podmanName = obj.value(QStringLiteral("podmanName")).toString();
    p.rpcUrl = obj.value(QStringLiteral("rpcUrl")).toString();
    if (p.targetType == TargetType::CustomRpc || p.targetType == TargetType::RemoteSsh) {
        p.rpcUser = obj.value(QStringLiteral("rpcUser")).toString();
        p.rpcPassword = obj.value(QStringLiteral("rpcPassword")).toString();
    }
    p.sshTarget = obj.value(QStringLiteral("sshTarget")).toString();
    p.sshPort = obj.value(QStringLiteral("sshPort")).toInt(22);
    if (p.sshPort <= 0 || p.sshPort > 65535) p.sshPort = 22;
    p.remoteBinaryPath = obj.value(QStringLiteral("remoteBinaryPath")).toString();

    p.coinbaseAddress = obj.value(QStringLiteral("coinbaseAddress")).toString().trimmed();
    p.threads = qMax(1, obj.value(QStringLiteral("threads")).toInt(1));
    p.cpuAffinity = obj.value(QStringLiteral("cpuAffinity")).toString().trimmed();
    p.maxTemp = qMax(0, obj.value(QStringLiteral("maxTemp")).toInt(0));
    p.gpuDevices = obj.value(QStringLiteral("gpuDevices")).toString().trimmed();
    p.gpuIntensity = obj.value(QStringLiteral("gpuIntensity")).toDouble(0.0);
    p.apiPort = qMax(0, obj.value(QStringLiteral("apiPort")).toInt(0));
    p.extraArgs = obj.value(QStringLiteral("extraArgs")).toString();
    p.autoRestart = obj.value(QStringLiteral("autoRestart")).toBool(true);
    p.disableInternalGenOnStart = obj.value(QStringLiteral("disableInternalGenOnStart")).toBool(false);
    p.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
    return p;
}

void WorkerRuntime::appendLogLine(const QString& sanitizedLine)
{
    if (sanitizedLine.isEmpty()) {
        return;
    }
    logLines.append(sanitizedLine);
    while (logLines.size() > kMaxLogLines) {
        logLines.removeFirst();
    }
}

void WorkerRuntime::resetRuntimeStats()
{
    pid = 0;
    wrapperPid = 0;
    adopted = false;
    hashrateKhs = 0.0;
    hashrateDisplay.clear();
    warningDeferred = false;
    warningDeferredMsg.clear();
    warningWalletLocked = false;
    warningWalletLockedMsg.clear();
    warningRpcError = false;
    warningRpcErrorMsg.clear();
    lastError.clear();
}
