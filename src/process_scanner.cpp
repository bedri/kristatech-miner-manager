#include "process_scanner.h"

#include "config_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include <cerrno>
#include <csignal>
#include <unistd.h>

namespace {

QStringList readProcCmdline(const QString& procPath)
{
    QFile f(procPath + QStringLiteral("/cmdline"));
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray raw = f.readAll();
    if (raw.isEmpty()) {
        return {};
    }
    const QList<QByteArray> parts = raw.split('\0');
    QStringList out;
    out.reserve(parts.size());
    for (const QByteArray& p : parts) {
        if (!p.isEmpty()) {
            out.append(QString::fromUtf8(p));
        }
    }
    return out;
}

qint64 readProcParentPid(const QString& procPath)
{
    QFile f(procPath + QStringLiteral("/stat"));
    if (!f.open(QIODevice::ReadOnly)) {
        return 0;
    }
    const QByteArray data = f.readAll();
    const int rparen = data.lastIndexOf(')');
    if (rparen < 0 || rparen + 2 >= data.size()) {
        return 0;
    }
    const QList<QByteArray> rest = data.mid(rparen + 2).trimmed().split(' ');
    // rest[0] = state, rest[1] = ppid
    if (rest.size() >= 2) {
        return rest[1].toLongLong();
    }
    return 0;
}

QString resolveProcStdoutPath(qint64 pid)
{
    const QString linkPath = QStringLiteral("/proc/%1/fd/1").arg(pid);
    QFileInfo fi(linkPath);
    if (!fi.isSymLink()) {
        return {};
    }
    const QString target = fi.symLinkTarget();
    if (target.startsWith(QStringLiteral("pipe:")) ||
        target.startsWith(QStringLiteral("socket:")) ||
        target.startsWith(QStringLiteral("/dev/"))) {
        return {};
    }
    return target;
}

void populateFromMinerConfigJson(const QString& cfgPath, DiscoveredProcess& out)
{
    QFile f(cfgPath);
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        return;
    }
    const QJsonObject obj = doc.object();
    if (out.coinbaseAddress.isEmpty()) {
        out.coinbaseAddress = obj.value(QStringLiteral("coinbase-addr")).toString().trimmed();
    }
    if (out.rpcUrl.isEmpty()) {
        out.rpcUrl = obj.value(QStringLiteral("url")).toString().trimmed();
    }
    if (obj.contains(QStringLiteral("threads"))) {
        out.threads = qMax(1, obj.value(QStringLiteral("threads")).toInt(out.threads));
    }
    if (out.gpuDevices.isEmpty() && obj.contains(QStringLiteral("devices"))) {
        out.gpuDevices = obj.value(QStringLiteral("devices")).toString().trimmed();
    }
    if (out.gpuIntensity <= 0.0 && obj.contains(QStringLiteral("intensity"))) {
        out.gpuIntensity = obj.value(QStringLiteral("intensity")).toDouble(0.0);
    }
}

} // namespace

ProcessScanner::ProcessScanner(QObject* parent)
    : QObject(parent)
{
}

ProcessScanner::~ProcessScanner()
{
    for (QProcess* p : findChildren<QProcess*>()) {
        p->disconnect(this);
        if (p->state() != QProcess::NotRunning) {
            p->kill();
            p->waitForFinished(300);
        }
    }
}

QString ProcessScanner::defaultCpuMinerPath()
{
    const QString preferred = QStringLiteral("/home/bedri/Coin-Projects/cpuminer-opt/cpuminer");
    if (QFileInfo::exists(preferred) && QFileInfo(preferred).isExecutable()) {
        return preferred;
    }
    const QString inPath = QStandardPaths::findExecutable(QStringLiteral("cpuminer"));
    return inPath.isEmpty() ? preferred : inPath;
}

QString ProcessScanner::defaultCcMinerPath()
{
    const QString preferred = QStringLiteral("/home/bedri/Coin-Projects/ccminer/ccminer");
    if (QFileInfo::exists(preferred) && QFileInfo(preferred).isExecutable()) {
        return preferred;
    }
    const QString inPath = QStandardPaths::findExecutable(QStringLiteral("ccminer"));
    return inPath.isEmpty() ? preferred : inPath;
}

bool ProcessScanner::parseCmdline(const QStringList& args, DiscoveredProcess& out)
{
    if (args.isEmpty()) {
        return false;
    }

    int minerArgIdx = -1;
    for (int i = 0; i < args.size(); ++i) {
        const QString base = QFileInfo(args[i]).fileName().toLower();
        if (base == QStringLiteral("cpuminer") || base.startsWith(QStringLiteral("cpuminer-"))) {
            out.engine = MinerEngine::CpuMiner;
            out.binaryPath = args[i];
            minerArgIdx = i;
            break;
        }
        if (base == QStringLiteral("ccminer") || base.startsWith(QStringLiteral("ccminer-"))) {
            out.engine = MinerEngine::CcMiner;
            out.binaryPath = args[i];
            minerArgIdx = i;
            break;
        }
    }
    if (minerArgIdx < 0) {
        return false;
    }

    // Ignore parent wrapper commands if parsing a direct cmdline is desired,
    // except extract nsenter container PID when present earlier in argv.
    QString configPath;
    for (int i = minerArgIdx + 1; i < args.size(); ++i) {
        const QString& a = args[i];
        auto takeNext = [&](QString& dst) {
            if (i + 1 < args.size()) {
                dst = args[++i];
            }
        };

        if (a.startsWith(QStringLiteral("--coinbase-addr="))) {
            out.coinbaseAddress = a.mid(16).trimmed();
        } else if (a == QStringLiteral("--coinbase-addr")) {
            takeNext(out.coinbaseAddress);
        } else if (a.startsWith(QStringLiteral("--url="))) {
            out.rpcUrl = a.mid(6).trimmed();
        } else if (a == QStringLiteral("--url") || a == QStringLiteral("-o")) {
            takeNext(out.rpcUrl);
        } else if (a.startsWith(QStringLiteral("--threads="))) {
            out.threads = qMax(1, a.mid(10).toInt());
        } else if (a == QStringLiteral("-t") || a == QStringLiteral("--threads")) {
            QString v;
            takeNext(v);
            out.threads = qMax(1, v.toInt());
        } else if (a.startsWith(QStringLiteral("-t")) && a.size() > 2 && a[2].isDigit()) {
            out.threads = qMax(1, a.mid(2).toInt());
        } else if (a.startsWith(QStringLiteral("--devices="))) {
            out.gpuDevices = a.mid(10).trimmed();
        } else if (a == QStringLiteral("-d") || a == QStringLiteral("--devices")) {
            takeNext(out.gpuDevices);
        } else if (a.startsWith(QStringLiteral("--intensity="))) {
            out.gpuIntensity = a.mid(12).toDouble();
        } else if (a == QStringLiteral("-i") || a == QStringLiteral("--intensity")) {
            QString v;
            takeNext(v);
            out.gpuIntensity = v.toDouble();
        } else if (a.startsWith(QStringLiteral("--config="))) {
            configPath = a.mid(9).trimmed();
        } else if (a == QStringLiteral("-c") || a == QStringLiteral("--config")) {
            takeNext(configPath);
        }
    }

    if (!configPath.isEmpty()) {
        populateFromMinerConfigJson(configPath, out);
    }

    return true;
}

QVector<PodmanContainerInfo> ProcessScanner::discoverPodmanContainers()
{
    QVector<PodmanContainerInfo> result;

    // Load previously assigned pod addresses if present
    QHash<QString, QString> savedPodAddrs;
    const QString podAddrPath = QDir::homePath() + QStringLiteral("/.kristatech/cpuminer_pod_addresses.txt");
    QFile af(podAddrPath);
    if (af.open(QIODevice::ReadOnly)) {
        const QStringList lines = QString::fromUtf8(af.readAll()).split(QLatin1Char('\n'));
        for (const QString& raw : lines) {
            const QStringList parts = raw.trimmed().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            if (parts.size() == 2) {
                savedPodAddrs.insert(parts[0], parts[1]);
            }
        }
    }

    QProcess ps;
    ps.start(QStringLiteral("podman"),
             {QStringLiteral("ps"), QStringLiteral("-a"),
              QStringLiteral("--filter"), QStringLiteral("name=krista-cold-"),
              QStringLiteral("--format"), QStringLiteral("json")});
    if (!ps.waitForFinished(5000) || ps.exitCode() != 0) {
        return result;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(ps.readAllStandardOutput());
    if (!doc.isArray()) {
        return result;
    }

    for (const QJsonValue& v : doc.array()) {
        if (!v.isObject()) continue;
        const QJsonObject obj = v.toObject();
        QString name;
        const QJsonValue namesVal = obj.value(QStringLiteral("Names"));
        if (namesVal.isArray() && !namesVal.toArray().isEmpty()) {
            name = namesVal.toArray().first().toString();
        } else if (namesVal.isString()) {
            name = namesVal.toString();
        }
        if (name.isEmpty()) continue;

        PodmanContainerInfo info;
        info.name = name;
        info.running = (obj.value(QStringLiteral("State")).toString().toLower() == QStringLiteral("running"));
        info.pid = static_cast<qint64>(obj.value(QStringLiteral("Pid")).toDouble(0));
        info.assignedAddress = savedPodAddrs.value(name);

        if (info.running && info.pid <= 0) {
            QProcess insp;
            insp.start(QStringLiteral("podman"),
                       {QStringLiteral("inspect"), name, QStringLiteral("--format"), QStringLiteral("{{.State.Pid}}")});
            if (insp.waitForFinished(3000) && insp.exitCode() == 0) {
                info.pid = QString::fromUtf8(insp.readAllStandardOutput()).trimmed().toLongLong();
            }
        }
        result.append(info);
    }

    std::sort(result.begin(), result.end(), [](const PodmanContainerInfo& a, const PodmanContainerInfo& b) {
        return a.name < b.name;
    });
    return result;
}

QVector<DiscoveredProcess> ProcessScanner::scanRunningMiners()
{
    QVector<DiscoveredProcess> out;

    // 1. Map container PID -> container name
    QHash<qint64, QString> containerPidToName;
    const QVector<PodmanContainerInfo> pods = discoverPodmanContainers();
    for (const PodmanContainerInfo& pod : pods) {
        if (pod.running && pod.pid > 0) {
            containerPidToName.insert(pod.pid, pod.name);
        }
    }

    // 2. Scan /proc entries once
    const QDir procDir(QStringLiteral("/proc"));
    const QStringList entries = procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    struct RawProc {
        qint64 pid{0};
        qint64 ppid{0};
        QStringList args;
    };
    QHash<qint64, RawProc> rawProcs;
    QVector<qint64> pids;

    for (const QString& entry : entries) {
        bool ok = false;
        const qint64 pid = entry.toLongLong(&ok);
        if (!ok || pid <= 0) continue;

        const QString pdir = QStringLiteral("/proc/") + entry;
        const QStringList args = readProcCmdline(pdir);
        if (args.isEmpty()) continue;

        RawProc rp;
        rp.pid = pid;
        rp.ppid = readProcParentPid(pdir);
        rp.args = args;
        rawProcs.insert(pid, rp);
        pids.append(pid);
    }

    // Map wrapper PID (e.g., podman unshare nsenter -t <podPid> -n ...) -> podName
    QHash<qint64, QString> wrapperPidToPod;
    for (qint64 pid : pids) {
        const RawProc& rp = rawProcs[pid];
        const QString base0 = QFileInfo(rp.args.first()).fileName();
        if (base0 == QStringLiteral("podman") && rp.args.contains(QStringLiteral("nsenter"))) {
            const int tIdx = rp.args.indexOf(QStringLiteral("-t"));
            if (tIdx >= 0 && tIdx + 1 < rp.args.size()) {
                const qint64 targetPodPid = rp.args[tIdx + 1].toLongLong();
                if (containerPidToName.contains(targetPodPid)) {
                    wrapperPidToPod.insert(pid, containerPidToName.value(targetPodPid));
                }
            }
        }
    }

    // Collect candidate log files from legacy cpuminer_logs if stdout symlink isn't direct
    const QString legacyLogDir = QDir::homePath() + QStringLiteral("/.kristatech/cpuminer_logs");

    for (qint64 pid : pids) {
        const RawProc& rp = rawProcs[pid];
        const QString base0 = QFileInfo(rp.args.first()).fileName().toLower();
        // Skip wrapper parent process itself (podman unshare / taskset)
        if (base0 != QStringLiteral("cpuminer") && base0 != QStringLiteral("ccminer") &&
            !base0.startsWith(QStringLiteral("cpuminer-")) && !base0.startsWith(QStringLiteral("ccminer-"))) {
            continue;
        }

        DiscoveredProcess dp;
        if (!parseCmdline(rp.args, dp)) {
            continue;
        }
        dp.pid = pid;
        dp.logPath = resolveProcStdoutPath(pid);

        // Walk up parent chain (up to 3 hops) to detect podman unshare wrapper
        qint64 curPpid = rp.ppid;
        for (int hop = 0; hop < 3 && curPpid > 1; ++hop) {
            if (wrapperPidToPod.contains(curPpid)) {
                dp.wrapperPid = curPpid;
                dp.targetType = TargetType::PodmanContainer;
                dp.podmanName = wrapperPidToPod.value(curPpid);
                break;
            }
            if (!rawProcs.contains(curPpid)) {
                break;
            }
            curPpid = rawProcs[curPpid].ppid;
        }

        // Heuristic fallback for legacy logs (~/.kristatech/cpuminer_logs/worker_*_<pod|local>.log)
        if (dp.logPath.isEmpty() && QDir(legacyLogDir).exists()) {
            const QString suffix = (dp.targetType == TargetType::PodmanContainer && !dp.podmanName.isEmpty())
                ? (dp.podmanName + QStringLiteral(".log"))
                : QStringLiteral("_local.log");
            const QStringList logFiles = QDir(legacyLogDir).entryList({QStringLiteral("*") + suffix}, QDir::Files);
            if (dp.targetType == TargetType::PodmanContainer && logFiles.size() == 1) {
                dp.logPath = legacyLogDir + QLatin1Char('/') + logFiles.first();
            } else if (!dp.coinbaseAddress.isEmpty()) {
                for (const QString& lf : logFiles) {
                    const QString fullPath = legacyLogDir + QLatin1Char('/') + lf;
                    QFile f(fullPath);
                    if (f.open(QIODevice::ReadOnly)) {
                        const QByteArray head = f.read(16384);
                        if (head.contains(dp.coinbaseAddress.toUtf8())) {
                            dp.logPath = fullPath;
                            break;
                        }
                    }
                }
            }
        }

        out.append(dp);
    }

    return out;
}

bool ProcessScanner::isPidAlive(qint64 pid)
{
    if (pid <= 1) {
        return false;
    }
    if (::kill(static_cast<pid_t>(pid), 0) == 0) {
        return true;
    }
    return errno == EPERM;
}

qint64 ProcessScanner::findChildMinerPid(qint64 parentPid)
{
    if (parentPid <= 1) {
        return 0;
    }
    const QDir procDir(QStringLiteral("/proc"));
    const QStringList entries = procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& entry : entries) {
        bool ok = false;
        const qint64 pid = entry.toLongLong(&ok);
        if (!ok || pid <= 1) continue;
        const QString pdir = QStringLiteral("/proc/") + entry;
        if (readProcParentPid(pdir) != parentPid) continue;
        const QStringList args = readProcCmdline(pdir);
        if (args.isEmpty()) continue;
        const QString base0 = QFileInfo(args.first()).fileName().toLower();
        if (base0.startsWith(QStringLiteral("cpuminer")) || base0.startsWith(QStringLiteral("ccminer"))) {
            return pid;
        }
    }
    return 0;
}

void ProcessScanner::queryNvidiaGpusAsync(std::function<void(const QVector<GpuTelemetry>& gpus)> cb)
{
    const QString nvidiaSmi = QStandardPaths::findExecutable(QStringLiteral("nvidia-smi"));
    if (nvidiaSmi.isEmpty()) {
        if (cb) cb({});
        return;
    }

    QProcess* proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [proc, cb](int exitCode, QProcess::ExitStatus) {
                const QByteArray out = proc->readAllStandardOutput();
                proc->deleteLater();
                QVector<GpuTelemetry> gpus;
                if (exitCode == 0) {
                    const QStringList lines = QString::fromUtf8(out).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                    for (const QString& raw : lines) {
                        const QStringList cols = raw.split(QLatin1Char(','));
                        if (cols.size() < 8) continue;
                        GpuTelemetry g;
                        g.index = cols[0].trimmed().toInt();
                        g.name = cols[1].trimmed();
                        g.tempC = cols[2].trimmed().toInt();
                        g.powerW = cols[3].trimmed().toDouble();
                        g.powerLimitW = cols[4].trimmed().toDouble();
                        g.utilPct = cols[5].trimmed().toInt();
                        g.memUsedMB = cols[6].trimmed().toInt();
                        g.memTotalMB = cols[7].trimmed().toInt();
                        gpus.append(g);
                    }
                }
                if (cb) cb(gpus);
            });
    proc->start(nvidiaSmi,
                {QStringLiteral("--query-gpu=index,name,temperature.gpu,power.draw,power.limit,utilization.gpu,memory.used,memory.total"),
                 QStringLiteral("--format=csv,noheader,nounits")});
}
