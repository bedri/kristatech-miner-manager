#include "worker_controller.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <csignal>
#endif

namespace {

QString shellQuote(const QString& s)
{
    QString escaped = s;
    escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

double scaleHashrateToKhs(double value, const QString& unit)
{
    const QString u = unit.trimmed().toLower();
    if (u.startsWith(QStringLiteral("gh"))) return value * 1000000.0;
    if (u.startsWith(QStringLiteral("mh"))) return value * 1000.0;
    if (u.startsWith(QStringLiteral("kh"))) return value;
    if (u.startsWith(QStringLiteral("th"))) return value * 1000000000.0;
    return value / 1000.0; // H/s
}

bool isValidCpuAffinity(const QString& affinity)
{
    static const QRegularExpression re(QStringLiteral("^[0-9,-]+$"));
    return re.match(affinity.trimmed()).hasMatch();
}

bool isValidPodmanName(const QString& name)
{
    static const QRegularExpression re(QStringLiteral("^[a-zA-Z0-9][a-zA-Z0-9_.-]{0,127}$"));
    return re.match(name.trimmed()).hasMatch();
}

#ifndef Q_OS_WIN
bool inspectPodmanContainer(const QString& podmanBin,
                            const QString& podName,
                            bool* runningOut,
                            qint64* pidOut,
                            QString* errOut)
{
    QProcess proc;
    proc.start(podmanBin,
               {QStringLiteral("inspect"), podName,
                QStringLiteral("--format"), QStringLiteral("{{.State.Running}} {{.State.Pid}}")});
    if (!proc.waitForFinished(5000)) {
        if (errOut) {
            *errOut = QStringLiteral("podman inspect timed out");
        }
        return false;
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        const QString serr = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        if (errOut) {
            *errOut = serr.isEmpty() ? QStringLiteral("exit code %1").arg(proc.exitCode()) : serr;
        }
        return false;
    }
    const QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    const QStringList parts = out.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (parts.size() < 2) {
        if (errOut) {
            *errOut = QStringLiteral("Unexpected podman inspect output: ") + out;
        }
        return false;
    }
    const bool running = (parts[0].compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
    bool okPid = false;
    const qint64 pid = parts[1].toLongLong(&okPid);
    if (runningOut) *runningOut = running;
    if (pidOut) *pidOut = okPid ? pid : 0;
    return true;
}
#endif

#ifdef Q_OS_WIN
DWORD_PTR parseCpuAffinityMask(const QString& spec)
{
    DWORD_PTR mask = 0;
    const QStringList parts = spec.trimmed().split(QLatin1Char(','), Qt::SkipEmptyParts);
    const int maxBits = static_cast<int>(sizeof(DWORD_PTR) * 8);
    for (const QString& part : parts) {
        const int dash = part.indexOf(QLatin1Char('-'));
        if (dash >= 0) {
            bool okA = false;
            bool okB = false;
            const int a = part.left(dash).toInt(&okA);
            const int b = part.mid(dash + 1).toInt(&okB);
            if (okA && okB && a >= 0 && b >= a) {
                for (int cpu = a; cpu <= b && cpu < maxBits; ++cpu) {
                    mask |= (DWORD_PTR(1) << cpu);
                }
            }
        } else {
            bool ok = false;
            const int cpu = part.toInt(&ok);
            if (ok && cpu >= 0 && cpu < maxBits) {
                mask |= (DWORD_PTR(1) << cpu);
            }
        }
    }
    return mask;
}

void applyWindowsCpuAffinity(qint64 pid, const QString& affinitySpec)
{
    const DWORD_PTR mask = parseCpuAffinityMask(affinitySpec);
    if (mask == 0 || pid <= 1) {
        return;
    }
    HANDLE hProc = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION,
                               FALSE, static_cast<DWORD>(pid));
    if (hProc) {
        SetProcessAffinityMask(hProc, mask);
        CloseHandle(hProc);
    }
}
#endif

void terminateProcessByPid(qint64 pid, bool force)
{
    if (pid <= 1) {
        return;
    }
#ifdef Q_OS_WIN
    if (!force) {
        const QString tk = QStandardPaths::findExecutable(QStringLiteral("taskkill"));
        if (!tk.isEmpty()) {
            QProcess proc;
            proc.setProgram(tk);
            proc.setArguments({QStringLiteral("/PID"), QString::number(pid), QStringLiteral("/T")});
            proc.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
                args->flags |= CREATE_NO_WINDOW;
            });
            if (proc.startDetached()) {
                return;
            }
        }
    }
    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (hProc) {
        TerminateProcess(hProc, 1);
        CloseHandle(hProc);
    }
#else
    ::kill(static_cast<pid_t>(pid), force ? SIGKILL : SIGTERM);
#endif
}

} // namespace

WorkerController::WorkerController(ConfigStore* store, RpcClient* rpc, QObject* parent)
    : QObject(parent)
    , m_store(store)
    , m_rpc(rpc)
{
    m_logTimer.setInterval(500);
    connect(&m_logTimer, &QTimer::timeout, this, &WorkerController::onLogPollTick);
    m_logTimer.start();

    m_healthTimer.setInterval(1000);
    connect(&m_healthTimer, &QTimer::timeout, this, &WorkerController::onHealthCheckTick);
    m_healthTimer.start();
}

WorkerController::~WorkerController()
{
    detachSshLogTail(QString());
    for (QProcess* proc : m_podmanStartProcs) {
        if (proc) {
            proc->disconnect(this);
            if (proc->state() != QProcess::NotRunning) {
                proc->terminate();
                if (!proc->waitForFinished(300)) {
                    proc->kill();
                    proc->waitForFinished(300);
                }
            }
        }
    }
    m_podmanStartProcs.clear();
}

WorkerProfile WorkerController::profileById(const QString& id) const
{
    const int idx = indexOfId(id);
    return idx >= 0 ? m_profiles[idx] : WorkerProfile{};
}

WorkerRuntime WorkerController::runtimeById(const QString& id) const
{
    return m_runtimes.value(id);
}

int WorkerController::indexOfId(const QString& id) const
{
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles[i].id == id) {
            return i;
        }
    }
    return -1;
}

QString WorkerController::makeUniqueId(const QString& base) const
{
    const QString stem = sanitizeWorkerId(base);
    if (indexOfId(stem) < 0) {
        return stem;
    }
    for (int n = 2; n < 10000; ++n) {
        const QString cand = stem + QLatin1Char('_') + QString::number(n);
        if (indexOfId(cand) < 0) {
            return cand;
        }
    }
    return stem + QLatin1Char('_') + QString::number(QDateTime::currentSecsSinceEpoch());
}

void WorkerController::loadInitialState(bool autoDiscoverIfEmpty)
{
    m_store->ensureDirectories();
    m_profiles = m_store->loadProfiles();
    for (const WorkerProfile& p : m_profiles) {
        if (!m_runtimes.contains(p.id)) {
            WorkerRuntime rt;
            rt.logFilePath = m_store->defaultLogPath(p.id);
            m_runtimes.insert(p.id, rt);
        }
    }

    if (m_profiles.isEmpty() && autoDiscoverIfEmpty) {
        autoDiscoverFleet();
    } else {
        adoptRunningProcesses(/*createProfilesForUnknown=*/false);
    }
    Q_EMIT workersChanged();
}

bool WorkerController::saveProfiles(QString* err)
{
    return m_store->saveProfiles(m_profiles, err);
}

QString WorkerController::addProfile(WorkerProfile profile)
{
    if (!isValidWorkerId(profile.id)) {
        profile.id = makeUniqueId(profile.name.isEmpty() ? QStringLiteral("worker") : profile.name);
    } else {
        profile.id = makeUniqueId(profile.id);
    }
    if (profile.name.trimmed().isEmpty()) {
        profile.name = profile.id;
    }
    m_profiles.append(profile);
    WorkerRuntime rt;
    rt.logFilePath = m_store->defaultLogPath(profile.id);
    m_runtimes.insert(profile.id, rt);
    saveProfiles();
    Q_EMIT workersChanged();
    return profile.id;
}

bool WorkerController::updateProfile(const WorkerProfile& profile)
{
    const int idx = indexOfId(profile.id);
    if (idx < 0) {
        return false;
    }
    m_profiles[idx] = profile;
    saveProfiles();
    Q_EMIT workersChanged();
    return true;
}

bool WorkerController::removeProfile(const QString& id, bool stopIfRunning)
{
    const int idx = indexOfId(id);
    if (idx < 0) {
        return false;
    }
    const WorkerRuntime rt = m_runtimes.value(id);
    if (stopIfRunning && rt.pid > 0 && m_profiles[idx].targetType != TargetType::RemoteSsh) {
        stopWorker(id);
    }
    m_podmanStartProcs.take(id);
    detachSshLogTail(id);
    QFile::remove(m_store->runtimeJsonPath(id));
    m_profiles.removeAt(idx);
    m_runtimes.remove(id);
    saveProfiles();
    Q_EMIT workersChanged();
    return true;
}

void WorkerController::importProfiles(const QVector<WorkerProfile>& imported, int* addedOut)
{
    int added = 0;
    for (WorkerProfile p : imported) {
        p.id = makeUniqueId(p.id.isEmpty() ? p.name : p.id);
        m_profiles.append(p);
        WorkerRuntime rt;
        rt.logFilePath = m_store->defaultLogPath(p.id);
        m_runtimes.insert(p.id, rt);
        ++added;
    }
    if (added > 0) {
        saveProfiles();
        adoptRunningProcesses(/*createProfilesForUnknown=*/false);
        Q_EMIT workersChanged();
    }
    if (addedOut) *addedOut = added;
}

QStringList WorkerController::splitExtraArgs(const QString& raw)
{
    QStringList out;
    QString current;
    QChar quote;
    bool escaping = false;

    for (const QChar ch : raw.trimmed()) {
        if (escaping) {
            current.append(ch);
            escaping = false;
            continue;
        }
        if (ch == QLatin1Char('\\') && quote != QLatin1Char('\'')) {
            escaping = true;
            continue;
        }
        if (!quote.isNull()) {
            if (ch == quote) {
                quote = QChar();
            } else {
                current.append(ch);
            }
            continue;
        }
        if (ch == QLatin1Char('\'') || ch == QLatin1Char('"')) {
            quote = ch;
            continue;
        }
        if (ch.isSpace()) {
            if (!current.isEmpty()) {
                out.append(current);
                current.clear();
            }
            continue;
        }
        current.append(ch);
    }
    if (!current.isEmpty()) {
        out.append(current);
    }
    return out;
}

void WorkerController::startWorker(const QString& id)
{
    const int idx = indexOfId(id);
    if (idx < 0) return;
    if (m_podmanStartProcs.contains(id)) return;

    const WorkerProfile& p = m_profiles[idx];
    WorkerRuntime& rt = m_runtimes[id];
    rt.resetRuntimeStats();
    rt.userStopped = false;
    rt.nextRetryEpochMs = 0;
    rt.status = WorkerStatus::Starting;
    rt.startedAtEpochMs = QDateTime::currentMSecsSinceEpoch();
    if (rt.logFilePath.isEmpty()) {
        rt.logFilePath = m_store->defaultLogPath(p.id);
    }

    if (p.targetType == TargetType::RemoteSsh) {
        startRemoteSsh(p, rt);
    } else {
        startLocalOrPodman(p, rt);
    }
    Q_EMIT workersChanged();
}

void WorkerController::startLocalOrPodman(const WorkerProfile& p, WorkerRuntime& rt)
{
    m_store->ensureDirectories();
    QString exePath = !p.binaryPath.trimmed().isEmpty()
        ? p.binaryPath.trimmed()
        : (p.engine == MinerEngine::CcMiner ? ProcessScanner::defaultCcMinerPath() : ProcessScanner::defaultCpuMinerPath());
    if (!QFileInfo::exists(exePath) && !exePath.contains(QLatin1Char('/')) && !exePath.contains(QLatin1Char('\\'))) {
        const QString inPath = QStandardPaths::findExecutable(exePath);
        if (!inPath.isEmpty()) {
            exePath = inPath;
        }
    }

    if (!QFileInfo::exists(exePath) || !QFileInfo(exePath).isExecutable()) {
        rt.status = WorkerStatus::Error;
        rt.lastError = QStringLiteral("Miner executable not found or not executable: ") + exePath;
        appendSanitizedLines(p.id, rt, {QStringLiteral("[KMM] ERROR: ") + rt.lastError});
        return;
    }

    const QString logPath = m_store->defaultLogPath(p.id);
    rt.logFilePath = logPath;
    if (!QFileInfo::exists(logPath)) {
        ConfigStore::writeFile0600(logPath, QByteArray());
    }
    const QFileInfo logInfo(logPath);
    rt.logReadOffset = logInfo.exists() ? logInfo.size() : 0;

#ifdef Q_OS_WIN
    if (p.targetType == TargetType::PodmanContainer) {
        rt.status = WorkerStatus::Error;
        rt.lastError = QStringLiteral("Podman container targets are only supported on Linux hosts.");
        appendSanitizedLines(p.id, rt, {QStringLiteral("[KMM] ERROR: ") + rt.lastError});
        return;
    }
    launchLocalOrPodmanWithPid(p, rt, exePath, 0);
#else
    if (p.targetType != TargetType::PodmanContainer) {
        launchLocalOrPodmanWithPid(p, rt, exePath, 0);
        return;
    }

    const QString podName = p.podmanName.trimmed();
    if (!isValidPodmanName(podName)) {
        rt.status = WorkerStatus::Error;
        rt.lastError = QStringLiteral("Invalid Podman container name: ") + podName;
        appendSanitizedLines(p.id, rt, {QStringLiteral("[KMM] ERROR: ") + rt.lastError});
        return;
    }

    const QString podmanBin = QStandardPaths::findExecutable(QStringLiteral("podman"));
    if (podmanBin.isEmpty()) {
        rt.status = WorkerStatus::Error;
        rt.lastError = QStringLiteral("podman executable not found in PATH");
        appendSanitizedLines(p.id, rt, {QStringLiteral("[KMM] ERROR: ") + rt.lastError});
        return;
    }

    bool running = false;
    qint64 podPid = 0;
    QString inspectErr;
    if (!inspectPodmanContainer(podmanBin, podName, &running, &podPid, &inspectErr)) {
        rt.status = WorkerStatus::Error;
        rt.lastError = QStringLiteral("Podman inspect failed for %1: %2").arg(podName, inspectErr);
        appendSanitizedLines(p.id, rt, {QStringLiteral("[KMM] ERROR: ") + rt.lastError});
        return;
    }

    if (running && podPid > 0) {
        launchLocalOrPodmanWithPid(p, rt, exePath, podPid);
        return;
    }

    rt.status = WorkerStatus::Starting;
    appendSanitizedLines(p.id, rt,
                         {QStringLiteral("[KMM] Podman container '%1' is stopped; starting container...").arg(podName)});

    QProcess* startProc = new QProcess(this);
    m_podmanStartProcs.insert(p.id, startProc);

    const QString workerId = p.id;
    connect(startProc, &QProcess::errorOccurred, this,
            [this, startProc, workerId, podName](QProcess::ProcessError err) {
                if (err != QProcess::FailedToStart) return;
                if (m_podmanStartProcs.value(workerId) != startProc) {
                    startProc->deleteLater();
                    return;
                }
                m_podmanStartProcs.remove(workerId);
                startProc->deleteLater();

                if (indexOfId(workerId) < 0 || !m_runtimes.contains(workerId)) return;
                WorkerRuntime& curRt = m_runtimes[workerId];
                if (curRt.userStopped) return;

                curRt.status = WorkerStatus::Error;
                curRt.lastError = QStringLiteral("Failed to launch podman start %1: %2")
                    .arg(podName, startProc->errorString());
                appendSanitizedLines(workerId, curRt, {QStringLiteral("[KMM] ERROR: ") + curRt.lastError});
                Q_EMIT workersChanged();
            });

    connect(startProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, startProc, workerId, podName, podmanBin, exePath](int exitCode, QProcess::ExitStatus exitStatus) {
                if (m_podmanStartProcs.value(workerId) != startProc) {
                    startProc->deleteLater();
                    return;
                }
                m_podmanStartProcs.remove(workerId);
                startProc->deleteLater();

                const int curIdx = indexOfId(workerId);
                if (curIdx < 0 || !m_runtimes.contains(workerId)) return;
                WorkerRuntime& curRt = m_runtimes[workerId];
                if (curRt.userStopped) return;

                if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                    const QString serr = QString::fromUtf8(startProc->readAllStandardError()).trimmed();
                    const QString sout = QString::fromUtf8(startProc->readAllStandardOutput()).trimmed();
                    const QString detail = !serr.isEmpty() ? serr
                        : (!sout.isEmpty() ? sout : QStringLiteral("exit code %1").arg(exitCode));
                    curRt.status = WorkerStatus::Error;
                    curRt.lastError = QStringLiteral("podman start %1 failed: %2").arg(podName, detail);
                    appendSanitizedLines(workerId, curRt, {QStringLiteral("[KMM] ERROR: ") + curRt.lastError});
                    Q_EMIT workersChanged();
                    return;
                }

                appendSanitizedLines(workerId, curRt,
                                     {QStringLiteral("[KMM] Podman container '%1' started.").arg(podName)});

                bool nowRunning = false;
                qint64 newPid = 0;
                QString reInspectErr;
                if (!inspectPodmanContainer(podmanBin, podName, &nowRunning, &newPid, &reInspectErr)
                    || !nowRunning || newPid <= 0) {
                    curRt.status = WorkerStatus::Error;
                    curRt.lastError = QStringLiteral("Podman container '%1' started but is not running (PID %2): %3")
                        .arg(podName)
                        .arg(newPid)
                        .arg(reInspectErr);
                    appendSanitizedLines(workerId, curRt, {QStringLiteral("[KMM] ERROR: ") + curRt.lastError});
                    Q_EMIT workersChanged();
                    return;
                }

                const WorkerProfile curProfile = m_profiles[curIdx];
                launchLocalOrPodmanWithPid(curProfile, curRt, exePath, newPid);
                Q_EMIT workersChanged();
            });

    startProc->start(podmanBin, {QStringLiteral("start"), podName});
#endif
}

void WorkerController::launchLocalOrPodmanWithPid(const WorkerProfile& p,
                                                  WorkerRuntime& rt,
                                                  const QString& exePath,
                                                  qint64 podPid)
{
    if (p.disableInternalGenOnStart && m_rpc && p.targetType != TargetType::RemoteSsh) {
        m_rpc->setInternalGenerate(p, false, nullptr);
    }

    const RpcCredentials creds = ConfigStore::resolveCredentials(p);
    if (!creds.valid) {
        rt.status = WorkerStatus::Error;
        rt.lastError = creds.error;
        appendSanitizedLines(p.id, rt, {QStringLiteral("[KMM] ERROR: ") + creds.error});
        return;
    }

    const QString cfgPath = m_store->runtimeJsonPath(p.id);
    QString writeErr;
    if (!ConfigStore::writeFile0600(cfgPath, ConfigStore::buildMinerRuntimeJson(p, creds), &writeErr)) {
        rt.status = WorkerStatus::Error;
        rt.lastError = writeErr;
        appendSanitizedLines(p.id, rt, {QStringLiteral("[KMM] ERROR writing config: ") + writeErr});
        return;
    }

    const QString logPath = rt.logFilePath.isEmpty() ? m_store->defaultLogPath(p.id) : rt.logFilePath;
    rt.logFilePath = logPath;
    if (!QFileInfo::exists(logPath)) {
        ConfigStore::writeFile0600(logPath, QByteArray());
    }

    QStringList minerCmdArgs;
    minerCmdArgs << exePath << QStringLiteral("-c") << cfgPath;
    if (!p.extraArgs.trimmed().isEmpty()) {
        minerCmdArgs << splitExtraArgs(p.extraArgs);
    }

    QString program;
    QStringList fullArgs;

#ifdef Q_OS_WIN
    Q_UNUSED(podPid);
    if (p.targetType == TargetType::PodmanContainer) {
        rt.status = WorkerStatus::Error;
        rt.lastError = QStringLiteral("Podman container targets are only supported on Linux hosts.");
        appendSanitizedLines(p.id, rt, {QStringLiteral("[KMM] ERROR: ") + rt.lastError});
        return;
    }
    program = minerCmdArgs.takeFirst();
    fullArgs = minerCmdArgs;
#else
    if (p.targetType == TargetType::PodmanContainer) {
        if (podPid <= 0) {
            rt.status = WorkerStatus::Error;
            rt.lastError = QStringLiteral("Podman container is not running: ") + p.podmanName;
            appendSanitizedLines(p.id, rt, {QStringLiteral("[KMM] ERROR: ") + rt.lastError});
            return;
        }
        const QString podmanBin = QStandardPaths::findExecutable(QStringLiteral("podman"));
        QStringList inner = {
            podmanBin.isEmpty() ? QStringLiteral("podman") : podmanBin,
            QStringLiteral("unshare"), QStringLiteral("nsenter"),
            QStringLiteral("-t"), QString::number(podPid), QStringLiteral("-n")
        };
        inner << minerCmdArgs;
        if (!p.cpuAffinity.trimmed().isEmpty() && isValidCpuAffinity(p.cpuAffinity)) {
            program = QStringLiteral("taskset");
            fullArgs << QStringLiteral("-c") << p.cpuAffinity.trimmed() << inner;
        } else {
            program = inner.takeFirst();
            fullArgs = inner;
        }
    } else {
        if (!p.cpuAffinity.trimmed().isEmpty() && isValidCpuAffinity(p.cpuAffinity)) {
            program = QStringLiteral("taskset");
            fullArgs << QStringLiteral("-c") << p.cpuAffinity.trimmed() << minerCmdArgs;
        } else {
            program = minerCmdArgs.takeFirst();
            fullArgs = minerCmdArgs;
        }
    }
#endif

    QProcess starter;
    starter.setProgram(program);
    starter.setArguments(fullArgs);
    starter.setStandardInputFile(QProcess::nullDevice());
    starter.setStandardOutputFile(logPath, QIODevice::Append);
    starter.setStandardErrorFile(logPath, QIODevice::Append);
#ifdef Q_OS_WIN
    starter.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif

    qint64 startedPid = 0;
    if (!starter.startDetached(&startedPid) || startedPid <= 0) {
        rt.status = WorkerStatus::Error;
        rt.lastError = QStringLiteral("Failed to start process: ") + program;
        appendSanitizedLines(p.id, rt, {QStringLiteral("[KMM] ERROR: ") + rt.lastError});
        return;
    }

#ifdef Q_OS_WIN
    if (!p.cpuAffinity.trimmed().isEmpty() && isValidCpuAffinity(p.cpuAffinity)) {
        applyWindowsCpuAffinity(startedPid, p.cpuAffinity);
    }
#endif

    if (p.targetType == TargetType::PodmanContainer) {
        rt.wrapperPid = startedPid;
        rt.pid = startedPid;
        const QString workerId = p.id;
        QTimer::singleShot(500, this, [this, workerId, startedPid]() {
            if (!m_runtimes.contains(workerId)) return;
            WorkerRuntime& cur = m_runtimes[workerId];
            if (cur.userStopped || cur.wrapperPid != startedPid) return;
            const qint64 child = ProcessScanner::findChildMinerPid(startedPid);
            if (child > 0) {
                cur.pid = child;
                Q_EMIT workersChanged();
            }
        });
    } else {
        rt.pid = startedPid;
    }

    rt.status = WorkerStatus::Waiting;
    rt.lastActiveEpochMs = QDateTime::currentMSecsSinceEpoch();
    appendSanitizedLines(p.id, rt,
                         {QStringLiteral("[KMM] Started (%1, PID %2)").arg(program).arg(startedPid)});
}

void WorkerController::startRemoteSsh(const WorkerProfile& p, WorkerRuntime& rt)
{
    if (!isValidWorkerId(p.id) || p.sshTarget.trimmed().isEmpty()) {
        rt.status = WorkerStatus::Error;
        rt.lastError = QStringLiteral("Invalid SSH target or worker ID");
        return;
    }
    const RpcCredentials creds = ConfigStore::resolveCredentials(p);
    if (!creds.valid) {
        rt.status = WorkerStatus::Error;
        rt.lastError = creds.error;
        appendSanitizedLines(p.id, rt, {QStringLiteral("[KMM] ERROR: ") + creds.error});
        return;
    }

    const QByteArray jsonBytes = ConfigStore::buildMinerRuntimeJson(p, creds);
    const QString portStr = QString::number(p.sshPort > 0 ? p.sshPort : 22);
    const QString remoteJson = QStringLiteral("~/.kristatech-mm/%1.json").arg(p.id);
    const QString remoteLog = QStringLiteral("~/.kristatech-mm/%1.log").arg(p.id);
    const QString remotePid = QStringLiteral("~/.kristatech-mm/%1.pid").arg(p.id);
    const QString remoteBin = p.remoteBinaryPath.trimmed().isEmpty()
        ? (p.engine == MinerEngine::CcMiner ? QStringLiteral("ccminer") : QStringLiteral("cpuminer"))
        : p.remoteBinaryPath.trimmed();

    QProcess* uploadProc = new QProcess(this);
    const QString uploadCmd = QStringLiteral("umask 077 && mkdir -p ~/.kristatech-mm && cat > ") + remoteJson;
    uploadProc->start(QStringLiteral("ssh"),
                      {QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
                       QStringLiteral("-o"), QStringLiteral("ConnectTimeout=8"),
                       QStringLiteral("-p"), portStr,
                       p.sshTarget.trimmed(), uploadCmd});
    uploadProc->write(jsonBytes);
    uploadProc->closeWriteChannel();

    const QString workerId = p.id;
    connect(uploadProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, uploadProc, workerId, p, remoteBin, remoteJson, remoteLog, remotePid, portStr](int code, QProcess::ExitStatus) {
                const QString uErr = QString::fromUtf8(uploadProc->readAllStandardError()).trimmed();
                uploadProc->deleteLater();
                if (!m_runtimes.contains(workerId)) return;
                WorkerRuntime& cur = m_runtimes[workerId];
                if (cur.userStopped) return;
                if (code != 0) {
                    cur.status = WorkerStatus::Error;
                    cur.lastError = QStringLiteral("SSH upload failed: ") + uErr;
                    appendSanitizedLines(workerId, cur, {QStringLiteral("[KMM] ERROR: ") + cur.lastError});
                    Q_EMIT workersChanged();
                    return;
                }

                QStringList quotedExtra;
                for (const QString& arg : splitExtraArgs(p.extraArgs)) {
                    quotedExtra.append(shellQuote(arg));
                }
                QString launchCmd;
                if (!p.cpuAffinity.trimmed().isEmpty() && isValidCpuAffinity(p.cpuAffinity)) {
                    launchCmd += QStringLiteral("taskset -c ") + shellQuote(p.cpuAffinity.trimmed()) + QLatin1Char(' ');
                }
                launchCmd += shellQuote(remoteBin) + QStringLiteral(" -c ") + remoteJson;
                if (!quotedExtra.isEmpty()) {
                    launchCmd += QLatin1Char(' ') + quotedExtra.join(QLatin1Char(' '));
                }
                const QString remoteWrap = QStringLiteral(
                    "nohup %1 </dev/null >%2 2>&1 & echo $! > %3 && sleep 0.3 && cat %3")
                    .arg(launchCmd, remoteLog, remotePid);

                QProcess* startProc = new QProcess(this);
                startProc->start(QStringLiteral("ssh"),
                                 {QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
                                  QStringLiteral("-o"), QStringLiteral("ConnectTimeout=8"),
                                  QStringLiteral("-p"), portStr,
                                  p.sshTarget.trimmed(), remoteWrap});
                connect(startProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                        this, [this, startProc, workerId, p](int scode, QProcess::ExitStatus) {
                            const QString out = QString::fromUtf8(startProc->readAllStandardOutput()).trimmed();
                            const QString serr = QString::fromUtf8(startProc->readAllStandardError()).trimmed();
                            startProc->deleteLater();
                            if (!m_runtimes.contains(workerId)) return;
                            WorkerRuntime& r = m_runtimes[workerId];
                            if (scode != 0 || out.toLongLong() <= 0) {
                                r.status = WorkerStatus::Error;
                                r.lastError = QStringLiteral("Remote SSH start failed: ") + (serr.isEmpty() ? out : serr);
                                appendSanitizedLines(workerId, r, {QStringLiteral("[KMM] ERROR: ") + r.lastError});
                            } else {
                                r.pid = out.toLongLong();
                                r.status = WorkerStatus::Waiting;
                                r.lastActiveEpochMs = QDateTime::currentMSecsSinceEpoch();
                                appendSanitizedLines(workerId, r,
                                                     {QStringLiteral("[KMM] Started remote worker on %1 (PID %2)")
                                                          .arg(p.sshTarget, out)});
                                attachSshLogTail(p);
                            }
                            Q_EMIT workersChanged();
                        });
            });
}

void WorkerController::attachSshLogTail(const WorkerProfile& p)
{
    detachSshLogTail(p.id);
    if (!isValidWorkerId(p.id) || p.sshTarget.trimmed().isEmpty()) return;

    QProcess* tailProc = new QProcess(this);
    const QString portStr = QString::number(p.sshPort > 0 ? p.sshPort : 22);
    const QString workerId = p.id;

    connect(tailProc, &QProcess::readyReadStandardOutput, this, [this, tailProc, workerId]() {
        if (!m_runtimes.contains(workerId)) return;
        const QString text = QString::fromUtf8(tailProc->readAllStandardOutput());
        const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        appendSanitizedLines(workerId, m_runtimes[workerId], lines);
    });
    connect(tailProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, tailProc, workerId](int, QProcess::ExitStatus) {
                if (m_sshTailProcs.value(workerId) == tailProc) {
                    m_sshTailProcs.remove(workerId);
                }
                tailProc->deleteLater();
            });

    m_sshTailProcs.insert(p.id, tailProc);
    tailProc->start(QStringLiteral("ssh"),
                    {QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
                     QStringLiteral("-o"), QStringLiteral("ServerAliveInterval=15"),
                     QStringLiteral("-p"), portStr,
                     p.sshTarget.trimmed(),
                     QStringLiteral("tail -n 60 -F ~/.kristatech-mm/%1.log").arg(p.id)});
}

void WorkerController::detachSshLogTail(const QString& id)
{
    if (id.isEmpty()) {
        for (QProcess* proc : m_sshTailProcs) {
            if (proc) {
                proc->terminate();
                proc->waitForFinished(500);
                proc->kill();
                proc->deleteLater();
            }
        }
        m_sshTailProcs.clear();
        return;
    }
    if (QProcess* proc = m_sshTailProcs.take(id)) {
        proc->terminate();
        proc->waitForFinished(300);
        if (proc->state() != QProcess::NotRunning) {
            proc->kill();
        }
        proc->deleteLater();
    }
}

void WorkerController::stopRemoteSsh(const WorkerProfile& p, WorkerRuntime& rt)
{
    detachSshLogTail(p.id);
    if (!isValidWorkerId(p.id) || p.sshTarget.trimmed().isEmpty()) return;

    const QString portStr = QString::number(p.sshPort > 0 ? p.sshPort : 22);
    const QString cmd = QStringLiteral(
        "if [ -f ~/.kristatech-mm/%1.pid ]; then "
        "P=$(cat ~/.kristatech-mm/%1.pid); "
        "kill -TERM \"$P\" 2>/dev/null || true; "
        "fi; rm -f ~/.kristatech-mm/%1.json ~/.kristatech-mm/%1.pid")
        .arg(p.id);

    QProcess* proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            proc, &QProcess::deleteLater);
    proc->start(QStringLiteral("ssh"),
                {QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
                 QStringLiteral("-o"), QStringLiteral("ConnectTimeout=6"),
                 QStringLiteral("-p"), portStr,
                 p.sshTarget.trimmed(), cmd});
    appendSanitizedLines(p.id, rt, {QStringLiteral("[KMM] Sent stop signal to remote host ") + p.sshTarget});
}

void WorkerController::stopWorker(const QString& id)
{
    const int idx = indexOfId(id);
    if (idx < 0) return;

    m_podmanStartProcs.take(id);
    const WorkerProfile& p = m_profiles[idx];
    WorkerRuntime& rt = m_runtimes[id];
    rt.userStopped = true;
    rt.nextRetryEpochMs = 0;

    if (p.targetType == TargetType::RemoteSsh) {
        stopRemoteSsh(p, rt);
        rt.status = WorkerStatus::Stopped;
        rt.pid = 0;
        rt.hashrateKhs = 0.0;
        rt.hashrateDisplay.clear();
        Q_EMIT workersChanged();
        return;
    }

    const qint64 minerPid = rt.pid;
    const qint64 wrapPid = rt.wrapperPid;
    if (minerPid > 1) {
        terminateProcessByPid(minerPid, /*force=*/false);
    }
    if (wrapPid > 1 && wrapPid != minerPid) {
        terminateProcessByPid(wrapPid, /*force=*/false);
    }

    if (minerPid > 1 || wrapPid > 1) {
        QTimer::singleShot(2500, this, [minerPid, wrapPid]() {
            if (minerPid > 1 && ProcessScanner::isPidAlive(minerPid)) {
                terminateProcessByPid(minerPid, /*force=*/true);
            }
            if (wrapPid > 1 && wrapPid != minerPid && ProcessScanner::isPidAlive(wrapPid)) {
                terminateProcessByPid(wrapPid, /*force=*/true);
            }
        });
    }

    QFile::remove(m_store->runtimeJsonPath(id));
    rt.status = WorkerStatus::Stopped;
    rt.pid = 0;
    rt.wrapperPid = 0;
    rt.adopted = false;
    rt.hashrateKhs = 0.0;
    rt.hashrateDisplay.clear();
    appendSanitizedLines(id, rt, {QStringLiteral("[KMM] Worker stopped.")});
    Q_EMIT workersChanged();
}

void WorkerController::restartWorker(const QString& id)
{
    stopWorker(id);
    QTimer::singleShot(450, this, [this, id]() {
        if (indexOfId(id) >= 0) {
            startWorker(id);
        }
    });
}

void WorkerController::startAll()
{
    for (const WorkerProfile& p : m_profiles) {
        if (!p.enabled) continue;
        if (m_podmanStartProcs.contains(p.id)) continue;
        const WorkerRuntime& rt = m_runtimes.value(p.id);
        if (rt.status == WorkerStatus::Starting) continue;
        if (rt.pid <= 0 || rt.status == WorkerStatus::Stopped || rt.status == WorkerStatus::Error) {
            startWorker(p.id);
        }
    }
}

void WorkerController::stopAll(bool includeRemoteSsh)
{
    for (const WorkerProfile& p : m_profiles) {
        if (!includeRemoteSsh && p.targetType == TargetType::RemoteSsh) {
            continue;
        }
        const WorkerRuntime& rt = m_runtimes.value(p.id);
        if (rt.pid > 0 || rt.status != WorkerStatus::Stopped) {
            stopWorker(p.id);
        }
    }
}

int WorkerController::countRunningLocalOrPodman() const
{
    int n = 0;
    for (const WorkerProfile& p : m_profiles) {
        if (p.targetType == TargetType::RemoteSsh) continue;
        const WorkerRuntime& rt = m_runtimes.value(p.id);
        if (rt.pid > 0 && rt.status != WorkerStatus::Stopped && rt.status != WorkerStatus::Error) {
            ++n;
        }
    }
    return n;
}

int WorkerController::countRunningUnregisteredOrDeferred() const
{
    if (!m_rpc) return 0;
    int n = 0;
    for (const WorkerProfile& p : m_profiles) {
        if (p.targetType == TargetType::RemoteSsh) continue;
        const WorkerRuntime& rt = m_runtimes.value(p.id);
        if (rt.pid <= 0 || rt.status == WorkerStatus::Stopped || rt.status == WorkerStatus::Error) continue;
        const PoolSeatInfo seat = m_rpc->seatInfoForAddress(p.coinbaseAddress);
        if ((seat.known && !seat.registered) || rt.warningDeferred) {
            ++n;
        }
    }
    return n;
}

int WorkerController::stopUnregisteredOrDeferred()
{
    if (!m_rpc) return 0;
    QStringList ids;
    for (const WorkerProfile& p : m_profiles) {
        if (p.targetType == TargetType::RemoteSsh) continue;
        const WorkerRuntime& rt = m_runtimes.value(p.id);
        if (rt.pid <= 0 || rt.status == WorkerStatus::Stopped || rt.status == WorkerStatus::Error) continue;
        const PoolSeatInfo seat = m_rpc->seatInfoForAddress(p.coinbaseAddress);
        if ((seat.known && !seat.registered) || rt.warningDeferred) {
            ids.append(p.id);
        }
    }
    for (const QString& id : ids) {
        stopWorker(id);
    }
    return ids.size();
}

bool WorkerController::isWorkerRunning(const WorkerProfile& p, const WorkerRuntime& rt) const
{
    if (rt.status == WorkerStatus::Stopped || rt.status == WorkerStatus::Error) {
        return false;
    }
    if (p.targetType == TargetType::RemoteSsh) {
        return !rt.userStopped;
    }
    return rt.pid > 0 || rt.status == WorkerStatus::Starting || rt.status == WorkerStatus::Restarting;
}

void WorkerController::autoStopIfDeferred(const QString& id)
{
    if (!m_autoStopOnDeferred) return;
    const int idx = indexOfId(id);
    if (idx < 0 || !m_runtimes.contains(id)) return;
    const WorkerProfile p = m_profiles[idx];
    const WorkerRuntime& rt = m_runtimes[id];
    if (!rt.warningDeferred || !isWorkerRunning(p, rt)) {
        return;
    }
    if (m_rpc && !p.coinbaseAddress.trimmed().isEmpty()) {
        const PoolSeatInfo seat = m_rpc->seatInfoForAddress(p.coinbaseAddress.trimmed());
        if (seat.known && seat.registered) {
            return;
        }
    }

    const QString reason = rt.warningDeferredMsg.isEmpty()
        ? QStringLiteral("ADAM Auto-Register deferred")
        : rt.warningDeferredMsg;
    stopWorker(id);
    appendSanitizedLines(id, m_runtimes[id],
                         {QStringLiteral("[KMM] Auto-stopped (Auto-Register Deferred): ") + reason});
    Q_EMIT workerAutoStopped(id, reason);
}

void WorkerController::setAutoStopOnDeferred(bool enabled)
{
    if (m_autoStopOnDeferred == enabled) {
        return;
    }
    m_autoStopOnDeferred = enabled;
    Q_EMIT autoStopOnDeferredChanged(enabled);
    if (enabled) {
        QStringList ids;
        ids.reserve(m_profiles.size());
        for (const WorkerProfile& p : m_profiles) {
            ids.append(p.id);
        }
        for (const QString& id : ids) {
            autoStopIfDeferred(id);
        }
    }
}

void WorkerController::clearWorkerLog(const QString& id)
{
    if (m_runtimes.contains(id)) {
        m_runtimes[id].logLines.clear();
        Q_EMIT workersChanged();
    }
}

bool WorkerController::matchesProcess(const WorkerProfile& p, const DiscoveredProcess& dp) const
{
    if (p.engine != dp.engine) return false;
    if (p.targetType != dp.targetType) return false;
    if (p.targetType == TargetType::PodmanContainer && p.podmanName != dp.podmanName) {
        return false;
    }
    if (!p.coinbaseAddress.isEmpty() && !dp.coinbaseAddress.isEmpty() &&
        p.coinbaseAddress == dp.coinbaseAddress) {
        return true;
    }
    if (p.targetType == TargetType::PodmanContainer && p.podmanName == dp.podmanName) {
        return true;
    }
    return false;
}

int WorkerController::adoptRunningProcesses(bool createProfilesForUnknown)
{
    int adoptedCount = 0;
    const QVector<DiscoveredProcess> live = ProcessScanner::scanRunningMiners();
    for (const DiscoveredProcess& dp : live) {
        int matchIdx = -1;
        for (int i = 0; i < m_profiles.size(); ++i) {
            if (matchesProcess(m_profiles[i], dp)) {
                matchIdx = i;
                break;
            }
        }
        if (matchIdx < 0 && createProfilesForUnknown) {
            WorkerProfile np;
            const QString addrShort = dp.coinbaseAddress.isEmpty() ? QString::number(dp.pid) : dp.coinbaseAddress.left(8);
            const QString baseName = (dp.targetType == TargetType::PodmanContainer && !dp.podmanName.isEmpty())
                ? dp.podmanName
                : (engineToString(dp.engine) + QStringLiteral("_local_") + addrShort);
            np.id = makeUniqueId(baseName);
            np.name = (dp.targetType == TargetType::PodmanContainer && !dp.podmanName.isEmpty())
                ? (dp.podmanName + QStringLiteral(" (") + addrShort + QStringLiteral(")"))
                : (QStringLiteral("Local ") + engineToString(dp.engine) + QStringLiteral(" (") + addrShort + QStringLiteral(")"));
            np.engine = dp.engine;
            np.binaryPath = dp.binaryPath;
            np.targetType = dp.targetType;
            if (dp.targetType == TargetType::LocalWallet) {
                np.confPath = ConfigStore::defaultKristaTechConfPath();
            } else if (dp.targetType == TargetType::PodmanContainer) {
                np.podmanName = dp.podmanName;
            }
            np.rpcUrl = dp.rpcUrl;
            np.coinbaseAddress = dp.coinbaseAddress;
            np.threads = dp.threads;
            np.gpuDevices = dp.gpuDevices;
            np.gpuIntensity = dp.gpuIntensity;
            m_profiles.append(np);
            matchIdx = m_profiles.size() - 1;
        }
        if (matchIdx < 0) continue;

        const QString id = m_profiles[matchIdx].id;
        WorkerRuntime& rt = m_runtimes[id];
        if (rt.pid == dp.pid && rt.pid > 0) {
            continue;
        }
        rt.pid = dp.pid;
        rt.wrapperPid = dp.wrapperPid;
        rt.adopted = true;
        rt.userStopped = false;
        rt.status = WorkerStatus::Adopted;
        rt.lastActiveEpochMs = QDateTime::currentMSecsSinceEpoch();
        if (!dp.logPath.isEmpty()) {
            if (rt.logFilePath != dp.logPath) {
                rt.logFilePath = dp.logPath;
                rt.logReadOffset = 0;
            }
            readLogIncremental(id, rt, /*tailWindowBytes=*/24576);
        }
        ++adoptedCount;
    }
    return adoptedCount;
}

void WorkerController::autoDiscoverFleet(std::function<void(int added, int adopted)> cb)
{
    const int prevCount = m_profiles.size();
    const int adopted = adoptRunningProcesses(/*createProfilesForUnknown=*/true);

    auto hasProfileFor = [this](TargetType t, const QString& pod, const QString& addr) -> bool {
        for (const WorkerProfile& p : m_profiles) {
            if (p.targetType != t) continue;
            if (t == TargetType::PodmanContainer && p.podmanName == pod) return true;
            if (t == TargetType::LocalWallet && !addr.isEmpty() && p.coinbaseAddress == addr) return true;
        }
        return false;
    };

    // Add stopped Podman containers not currently running a miner
    const QVector<PodmanContainerInfo> pods = ProcessScanner::discoverPodmanContainers();
    for (const PodmanContainerInfo& pod : pods) {
        if (!hasProfileFor(TargetType::PodmanContainer, pod.name, pod.assignedAddress)) {
            WorkerProfile p;
            p.id = makeUniqueId(pod.name);
            p.name = pod.name;
            p.engine = MinerEngine::CpuMiner;
            p.binaryPath = ProcessScanner::defaultCpuMinerPath();
            p.targetType = TargetType::PodmanContainer;
            p.podmanName = pod.name;
            p.coinbaseAddress = pod.assignedAddress;
            p.threads = 1;
            p.enabled = true;
            m_profiles.append(p);
            WorkerRuntime rt;
            rt.logFilePath = m_store->defaultLogPath(p.id);
            m_runtimes.insert(p.id, rt);
        }
    }

    const QString localConf = ConfigStore::defaultKristaTechConfPath();
    if (!m_rpc) {
        const int added = m_profiles.size() - prevCount;
        if (added > 0) saveProfiles();
        Q_EMIT workersChanged();
        Q_EMIT discoveryFinished(added, adopted);
        if (cb) cb(added, adopted);
        return;
    }

    m_rpc->discoverLocalRegisteredSeats(localConf, [this, prevCount, adopted, localConf, hasProfileFor, cb](QVector<QString> seats) {
        for (const QString& addr : seats) {
            const PoolSeatInfo seat = m_rpc->seatInfoForAddress(addr);
            // Skip VPS Masternodes which do not mine via local cpuminer RPC
            if (seat.track == QStringLiteral("masternode")) {
                continue;
            }
            if (!hasProfileFor(TargetType::LocalWallet, QString(), addr)) {
                WorkerProfile p;
                p.id = makeUniqueId(QStringLiteral("local_") + addr.left(8));
                p.name = QStringLiteral("Local ") + seat.track + QStringLiteral(" (") + addr.left(8) + QStringLiteral(")");
                p.engine = MinerEngine::CpuMiner;
                p.binaryPath = ProcessScanner::defaultCpuMinerPath();
                p.targetType = TargetType::LocalWallet;
                p.confPath = localConf;
                p.coinbaseAddress = addr;
                p.threads = 1;
                m_profiles.append(p);
                WorkerRuntime rt;
                rt.logFilePath = m_store->defaultLogPath(p.id);
                m_runtimes.insert(p.id, rt);
            }
        }
        const int added = m_profiles.size() - prevCount;
        saveProfiles();
        Q_EMIT workersChanged();
        Q_EMIT discoveryFinished(added, adopted);
        if (cb) cb(added, adopted);
    });
}

void WorkerController::stopAllManagedAndCleanup()
{
    m_logTimer.stop();
    m_healthTimer.stop();
    detachSshLogTail(QString());

    for (const WorkerProfile& p : m_profiles) {
        if (p.targetType == TargetType::RemoteSsh) continue;
        WorkerRuntime& rt = m_runtimes[p.id];
#ifdef Q_OS_WIN
        const bool forceShutdown = true;
#else
        const bool forceShutdown = false;
#endif
        if (rt.pid > 1) {
            terminateProcessByPid(rt.pid, forceShutdown);
        }
        if (rt.wrapperPid > 1 && rt.wrapperPid != rt.pid) {
            terminateProcessByPid(rt.wrapperPid, forceShutdown);
        }
        rt.status = WorkerStatus::Stopped;
        rt.pid = 0;
        rt.wrapperPid = 0;
        QFile::remove(m_store->runtimeJsonPath(p.id));
    }
    saveProfiles();
}

void WorkerController::detachAll()
{
    m_logTimer.stop();
    m_healthTimer.stop();
    detachSshLogTail(QString());
    saveProfiles();
}

void WorkerController::appendSanitizedLines(const QString& id, WorkerRuntime& rt, const QStringList& lines)
{
    QStringList appended;
    appended.reserve(lines.size());
    for (const QString& raw : lines) {
        const QString clean = ConfigStore::sanitizeLogLine(raw);
        if (clean.isEmpty()) continue;
        parseLogLine(clean, rt);
        rt.appendLogLine(clean);
        appended.append(clean);
    }
    if (!appended.isEmpty()) {
        Q_EMIT workerLogAppended(id, appended);
    }
    if (m_autoStopOnDeferred && rt.warningDeferred && !rt.userStopped
        && rt.status != WorkerStatus::Stopped && rt.status != WorkerStatus::Error) {
        autoStopIfDeferred(id);
    }
}

void WorkerController::readLogIncremental(const QString& id, WorkerRuntime& rt, qint64 tailWindowBytes)
{
    if (rt.logFilePath.isEmpty()) return;
    QFile f(rt.logFilePath);
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    const qint64 fsize = f.size();
    if (fsize < rt.logReadOffset) {
        rt.logReadOffset = 0;
    }
    if (tailWindowBytes > 0 && rt.logReadOffset == 0 && fsize > tailWindowBytes) {
        rt.logReadOffset = fsize - tailWindowBytes;
    }
    if (fsize == rt.logReadOffset) {
        return;
    }
    f.seek(rt.logReadOffset);
    const QByteArray chunk = f.read(131072);
    rt.logReadOffset = f.pos();

    const QString text = QString::fromUtf8(chunk);
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    appendSanitizedLines(id, rt, lines);
}

void WorkerController::onLogPollTick()
{
    bool anyUpdate = false;
    for (const WorkerProfile& p : m_profiles) {
        if (p.targetType == TargetType::RemoteSsh) continue;
        WorkerRuntime& rt = m_runtimes[p.id];
        if (rt.pid <= 0 && (rt.status == WorkerStatus::Stopped
                            || rt.status == WorkerStatus::Starting
                            || rt.status == WorkerStatus::Error)) continue;
        const qint64 prevOff = rt.logReadOffset;
        readLogIncremental(p.id, rt, /*tailWindowBytes=*/24576);
        if (rt.logReadOffset != prevOff) {
            anyUpdate = true;
        }
    }
    if (anyUpdate) {
        Q_EMIT workersChanged();
    }
}

void WorkerController::onHealthCheckTick()
{
    ++m_healthTickCounter;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool changed = false;

    for (const WorkerProfile& p : m_profiles) {
        if (p.targetType == TargetType::RemoteSsh) {
            continue;
        }
        WorkerRuntime& rt = m_runtimes[p.id];

        if (rt.status == WorkerStatus::Restarting && rt.nextRetryEpochMs > 0 && now >= rt.nextRetryEpochMs) {
            rt.nextRetryEpochMs = 0;
            startWorker(p.id);
            changed = true;
            continue;
        }

        if (rt.pid > 0 && rt.status != WorkerStatus::Stopped) {
            if (ProcessScanner::isPidAlive(rt.pid)) {
                if (rt.startedAtEpochMs > 0 && (now - rt.startedAtEpochMs) > 60000 && rt.restartAttempts > 0) {
                    rt.restartAttempts = 0;
                }
            } else {
                const qint64 deadPid = rt.pid;
                rt.pid = 0;
                rt.wrapperPid = 0;
                rt.hashrateKhs = 0.0;
                rt.hashrateDisplay.clear();

                if (!rt.userStopped && p.autoRestart && p.enabled) {
                    rt.restartAttempts = qMin(rt.restartAttempts + 1, 6);
                    const qint64 delayMs = qMin<qint64>(30000, 3000 * (1LL << (rt.restartAttempts - 1)));
                    rt.nextRetryEpochMs = now + delayMs;
                    rt.status = WorkerStatus::Restarting;
                    appendSanitizedLines(p.id, rt,
                                         {QStringLiteral("[KMM] Process PID %1 exited; restarting in %2s (attempt #%3)")
                                              .arg(deadPid)
                                              .arg(delayMs / 1000)
                                              .arg(rt.restartAttempts)});
                } else {
                    rt.status = WorkerStatus::Error;
                    rt.lastError = QStringLiteral("Process exited unexpectedly");
                }
                changed = true;
            }
        }
    }

    // Periodic SSH PID verification every 15 seconds
    if ((m_healthTickCounter % 15) == 0) {
        for (const WorkerProfile& p : m_profiles) {
            if (p.targetType != TargetType::RemoteSsh) continue;
            const WorkerRuntime& rt = m_runtimes.value(p.id);
            if (rt.userStopped || rt.status == WorkerStatus::Stopped || p.sshTarget.trimmed().isEmpty()) continue;

            const QString id = p.id;
            QProcess* chk = new QProcess(this);
            const QString portStr = QString::number(p.sshPort > 0 ? p.sshPort : 22);
            const QString remoteCmd = QStringLiteral(
                "if [ -f ~/.kristatech-mm/%1.pid ] && kill -0 $(cat ~/.kristatech-mm/%1.pid) 2>/dev/null; then "
                "cat ~/.kristatech-mm/%1.pid; else echo DEAD; fi").arg(id);
            connect(chk, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                    this, [this, chk, id](int code, QProcess::ExitStatus) {
                        const QString out = QString::fromUtf8(chk->readAllStandardOutput()).trimmed();
                        chk->deleteLater();
                        if (!m_runtimes.contains(id) || code != 0) return;
                        WorkerRuntime& r = m_runtimes[id];
                        if (out == QStringLiteral("DEAD") && !r.userStopped) {
                            r.status = WorkerStatus::Error;
                            r.pid = 0;
                            r.hashrateKhs = 0.0;
                            Q_EMIT workersChanged();
                        } else if (out.toLongLong() > 0) {
                            r.pid = out.toLongLong();
                        }
                    });
            chk->start(QStringLiteral("ssh"),
                       {QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
                        QStringLiteral("-o"), QStringLiteral("ConnectTimeout=5"),
                        QStringLiteral("-p"), portStr,
                        p.sshTarget.trimmed(), remoteCmd});
        }
    }

    if (changed) {
        Q_EMIT workersChanged();
    }
}

void WorkerController::parseLogLine(const QString& sanitizedLine, WorkerRuntime& rt)
{
    static const QRegularExpression reNewBlock(QStringLiteral("New Block\\s+(\\d+)"));
    if (const auto m = reNewBlock.match(sanitizedLine); m.hasMatch()) {
        rt.lastBlock = m.captured(1).toLongLong();
        rt.warningRpcError = false;
        rt.warningRpcErrorMsg.clear();
        if (rt.status == WorkerStatus::Waiting || rt.status == WorkerStatus::Starting || rt.status == WorkerStatus::Adopted) {
            rt.status = WorkerStatus::Solving;
        }
    }

    static const QRegularExpression reWaiting(QStringLiteral("Waiting for next block or election"));
    if (reWaiting.match(sanitizedLine).hasMatch()) {
        rt.status = WorkerStatus::Waiting;
        rt.hashrateKhs = 0.0;
        rt.hashrateDisplay.clear();
    }

    // cpuminer-opt: Miner TTF @ 36.12 kh/s
    static const QRegularExpression reMinerTtf(QStringLiteral("Miner TTF @\\s*([0-9.]+)\\s*([kMGT]?[hH]/s)"));
    if (const auto m = reMinerTtf.match(sanitizedLine); m.hasMatch()) {
        const double val = m.captured(1).toDouble();
        const QString unit = m.captured(2);
        rt.hashrateKhs = scaleHashrateToKhs(val, unit);
        rt.hashrateDisplay = formatHashrateKhs(rt.hashrateKhs);
        rt.status = WorkerStatus::Solving;
    }

    // cpuminer-opt Total / ccminer GPU or accepted line hashrate (explicitly avoids Net TTF)
    static const QRegularExpression reTotalHr(QStringLiteral(
        R"((?:Total:\s*|GPU #\d+:[^\n]*?,\s*|accepted:\s*\d+/\d+\s*(?:\([^)]*\))?,\s*)([0-9.]+)\s*([kMGT]?[hH]/s))"));
    if (const auto m = reTotalHr.match(sanitizedLine); m.hasMatch()) {
        const double val = m.captured(1).toDouble();
        const QString unit = m.captured(2);
        rt.hashrateKhs = scaleHashrateToKhs(val, unit);
        rt.hashrateDisplay = formatHashrateKhs(rt.hashrateKhs);
        rt.status = WorkerStatus::Solving;
    }

    // cpuminer-opt block solve stats: "3 A3 S0 R0 BLOCK SOLVED"
    static const QRegularExpression reSolved(QStringLiteral("\\b(\\d+)\\s+A(\\d+)\\s+S(\\d+)\\s+R(\\d+)\\s+BLOCK SOLVED"));
    if (const auto m = reSolved.match(sanitizedLine); m.hasMatch()) {
        rt.accepted = m.captured(2).toInt();
        rt.stale = m.captured(3).toInt();
        rt.rejected = m.captured(4).toInt();
    }

    // ccminer accepted line: "accepted: 4/5 (80.00%), 125.40 MH/s yes!"
    static const QRegularExpression reCcAccepted(QStringLiteral("accepted:\\s*(\\d+)/(\\d+)"));
    if (const auto m = reCcAccepted.match(sanitizedLine); m.hasMatch()) {
        const int acc = m.captured(1).toInt();
        const int tot = m.captured(2).toInt();
        rt.accepted = acc;
        rt.rejected = qMax(0, tot - acc);
    }

    // deferred auto-register warning
    static const QRegularExpression reDeferred(QStringLiteral("ADAM Auto-Register deferred:\\s*(.+)$"));
    if (const auto m = reDeferred.match(sanitizedLine); m.hasMatch()) {
        rt.warningDeferred = true;
        rt.warningDeferredMsg = m.captured(1).trimmed();
    }
    static const QRegularExpression reRegActive(
        QStringLiteral("ADAM Auto-Register:.*(?:is active in ADAM pool|registered successfully)"),
        QRegularExpression::CaseInsensitiveOption);
    if (reRegActive.match(sanitizedLine).hasMatch()) {
        rt.warningDeferred = false;
        rt.warningDeferredMsg.clear();
    }

    // CPU temp
    static const QRegularExpression reCpuTemp(QStringLiteral("CPU temp:\\s*curr\\s*(\\d+)\\s*C"));
    if (const auto m = reCpuTemp.match(sanitizedLine); m.hasMatch()) {
        rt.cpuTempC = m.captured(1).toInt();
    }

    // RPC / HTTP failures
    static const QRegularExpression reRpcErr(QStringLiteral("(HTTP request failed|Authentication failed|JSON-RPC call failed|connect\\([^)]*\\)\\s*failed)"));
    if (reRpcErr.match(sanitizedLine).hasMatch()) {
        rt.warningRpcError = true;
        rt.warningRpcErrorMsg = sanitizedLine;
    }
}
