#include "config_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#ifndef Q_OS_WIN
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

ConfigStore::ConfigStore(const QString& baseDir)
    : m_baseDir(baseDir.isEmpty() ? defaultConfigDir() : baseDir)
{
}

QString ConfigStore::defaultConfigDir()
{
    const QString cfgHome = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (!cfgHome.isEmpty()) {
        return cfgHome + QStringLiteral("/KristaTechMinerManager");
    }
    return QDir::homePath() + QStringLiteral("/.config/KristaTechMinerManager");
}

QString ConfigStore::profilesFilePath() const
{
    return m_baseDir + QStringLiteral("/profiles.json");
}

QString ConfigStore::runtimeDirPath() const
{
    return m_baseDir + QStringLiteral("/run");
}

QString ConfigStore::logsDirPath() const
{
    return m_baseDir + QStringLiteral("/logs");
}

QString ConfigStore::runtimeJsonPath(const QString& workerId) const
{
    const QString safeId = isValidWorkerId(workerId) ? workerId : sanitizeWorkerId(workerId);
    return runtimeDirPath() + QLatin1Char('/') + safeId + QStringLiteral(".json");
}

QString ConfigStore::defaultLogPath(const QString& workerId) const
{
    const QString safeId = isValidWorkerId(workerId) ? workerId : sanitizeWorkerId(workerId);
    return logsDirPath() + QLatin1Char('/') + safeId + QStringLiteral(".log");
}

bool ConfigStore::ensureDirectories() const
{
    auto ensureDir0700 = [](const QString& dirPath) -> bool {
        QDir d;
        if (!d.exists(dirPath) && !d.mkpath(dirPath)) {
            return false;
        }
        QFile::setPermissions(dirPath,
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        return true;
    };

    return ensureDir0700(m_baseDir) && ensureDir0700(runtimeDirPath()) && ensureDir0700(logsDirPath());
}

bool ConfigStore::writeFile0600(const QString& path, const QByteArray& content, QString* errorOut)
{
    const QFileInfo fi(path);
    const QString parent = fi.dir().absolutePath();
    if (!QDir().exists(parent) && !QDir().mkpath(parent)) {
        if (errorOut) *errorOut = QStringLiteral("Failed to create directory: ") + parent;
        return false;
    }

#ifdef Q_OS_WIN
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorOut) *errorOut = file.errorString();
        return false;
    }
    if (file.write(content) != content.size()) {
        if (errorOut) *errorOut = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (errorOut) *errorOut = file.errorString();
        return false;
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
#else
    const QString tmpPath = path + QStringLiteral(".tmp.%1").arg(static_cast<qint64>(::getpid()));
    const QByteArray tmpLocal = tmpPath.toLocal8Bit();
    int fd = ::open(tmpLocal.constData(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        if (errorOut) *errorOut = QString::fromLocal8Bit(std::strerror(errno));
        return false;
    }

    qint64 written = 0;
    while (written < content.size()) {
        ssize_t n = ::write(fd, content.constData() + written, static_cast<size_t>(content.size() - written));
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errorOut) *errorOut = QString::fromLocal8Bit(std::strerror(errno));
            ::close(fd);
            ::unlink(tmpLocal.constData());
            return false;
        }
        written += n;
    }

    ::fsync(fd);
    ::fchmod(fd, S_IRUSR | S_IWUSR);
    ::close(fd);

    const QByteArray dstLocal = path.toLocal8Bit();
    if (::rename(tmpLocal.constData(), dstLocal.constData()) != 0) {
        if (errorOut) *errorOut = QString::fromLocal8Bit(std::strerror(errno));
        ::unlink(tmpLocal.constData());
        return false;
    }
    return true;
#endif
}

QString ConfigStore::defaultKristaTechConfPath()
{
#ifdef Q_OS_WIN
    const QString appData = qEnvironmentVariable("APPDATA");
    const QString roaming = appData.isEmpty()
        ? (QDir::homePath() + QStringLiteral("/AppData/Roaming"))
        : appData;
    const QString winPath = QDir(roaming).filePath(QStringLiteral("KristaTech/kristatech.conf"));
    if (QFileInfo::exists(winPath)) {
        return winPath;
    }
    const QString dotPath = QDir::homePath() + QStringLiteral("/.kristatech/kristatech.conf");
    if (QFileInfo::exists(dotPath)) {
        return dotPath;
    }
    return winPath;
#else
    return QDir::homePath() + QStringLiteral("/.kristatech/kristatech.conf");
#endif
}

QVector<WorkerProfile> ConfigStore::loadProfiles(QString* errorOut) const
{
    QFile f(profilesFilePath());
    if (!f.exists()) {
        return {};
    }
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorOut) *errorOut = f.errorString();
        return {};
    }
    return parseImportedProfilesJson(f.readAll(), errorOut);
}

bool ConfigStore::saveProfiles(const QVector<WorkerProfile>& profiles, QString* errorOut) const
{
    if (!ensureDirectories()) {
        if (errorOut) *errorOut = QStringLiteral("Unable to initialize config directory: ") + m_baseDir;
        return false;
    }
    const QByteArray data = exportProfilesJson(profiles, /*redactSecrets=*/false);
    return writeFile0600(profilesFilePath(), data, errorOut);
}

QByteArray ConfigStore::exportProfilesJson(const QVector<WorkerProfile>& profiles, bool redactSecrets) const
{
    QJsonArray arr;
    for (WorkerProfile p : profiles) {
        if (redactSecrets) {
            p.rpcPassword.clear();
        }
        arr.append(p.toJson());
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("workers"), arr);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QVector<WorkerProfile> ConfigStore::parseImportedProfilesJson(const QByteArray& data, QString* errorOut) const
{
    QJsonParseError jerr{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &jerr);
    if (jerr.error != QJsonParseError::NoError) {
        if (errorOut) *errorOut = jerr.errorString();
        return {};
    }

    QJsonArray arr;
    if (doc.isObject()) {
        arr = doc.object().value(QStringLiteral("workers")).toArray();
    } else if (doc.isArray()) {
        arr = doc.array();
    } else {
        if (errorOut) *errorOut = QStringLiteral("Invalid profiles JSON structure");
        return {};
    }

    QVector<WorkerProfile> out;
    out.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        if (!v.isObject()) continue;
        WorkerProfile p = WorkerProfile::fromJson(v.toObject());
        if (!p.id.isEmpty()) {
            out.append(p);
        }
    }
    return out;
}

RpcCredentials ConfigStore::parseKristaTechConf(const QString& confContent, int defaultPort)
{
    RpcCredentials creds;
    int port = defaultPort;
    bool isTestnet = false;
    bool explicitPort = false;

    const QStringList lines = confContent.split(QLatin1Char('\n'));
    for (const QString& raw : lines) {
        QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const int eqIdx = line.indexOf(QLatin1Char('='));
        if (eqIdx <= 0) {
            continue;
        }
        const QString key = line.left(eqIdx).trimmed().toLower();
        const QString val = line.mid(eqIdx + 1).trimmed();
        if (key == QStringLiteral("rpcuser")) {
            creds.user = val;
        } else if (key == QStringLiteral("rpcpassword")) {
            creds.password = val;
        } else if (key == QStringLiteral("rpcport")) {
            bool ok = false;
            const int parsed = val.toInt(&ok);
            if (ok && parsed > 0 && parsed <= 65535) {
                port = parsed;
                explicitPort = true;
            }
        } else if (key == QStringLiteral("testnet")) {
            isTestnet = (val == QStringLiteral("1") || val.toLower() == QStringLiteral("true"));
        }
    }

    if (isTestnet && !explicitPort && defaultPort == 27998) {
        port = 28998;
    }

    creds.url = QStringLiteral("http://127.0.0.1:%1/").arg(port);
    creds.valid = !creds.user.isEmpty() && !creds.password.isEmpty();
    if (!creds.valid) {
        creds.error = QStringLiteral("Missing rpcuser or rpcpassword in kristatech.conf");
    }
    return creds;
}

RpcCredentials ConfigStore::resolveCredentials(const WorkerProfile& profile)
{
    if (profile.targetType == TargetType::LocalWallet) {
        const QString path = profile.confPath.isEmpty()
            ? defaultKristaTechConfPath()
            : profile.confPath;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            RpcCredentials bad;
            bad.error = QStringLiteral("Cannot read ") + path + QStringLiteral(": ") + f.errorString();
            return bad;
        }
        return parseKristaTechConf(QString::fromUtf8(f.readAll()), 27998);
    }

    if (profile.targetType == TargetType::PodmanContainer) {
        if (profile.podmanName.trimmed().isEmpty()) {
            RpcCredentials bad;
            bad.error = QStringLiteral("Podman container name is empty");
            return bad;
        }
        const QString podmanBin = QStandardPaths::findExecutable(QStringLiteral("podman"));
        if (podmanBin.isEmpty()) {
            RpcCredentials bad;
            bad.error = QStringLiteral("podman executable not found in PATH");
            return bad;
        }
        QProcess proc;
        proc.start(podmanBin,
                   {QStringLiteral("exec"), profile.podmanName.trimmed(),
                    QStringLiteral("cat"), QStringLiteral("/data/kristatech.conf")});
        if (!proc.waitForFinished(5000) || proc.exitCode() != 0) {
            RpcCredentials bad;
            bad.error = QStringLiteral("podman exec cat /data/kristatech.conf failed: ")
                + QString::fromUtf8(proc.readAllStandardError()).trimmed();
            return bad;
        }
        return parseKristaTechConf(QString::fromUtf8(proc.readAllStandardOutput()), 27979);
    }

    RpcCredentials creds;
    creds.url = profile.rpcUrl.trimmed();
    creds.user = profile.rpcUser;
    creds.password = profile.rpcPassword;
    creds.valid = !creds.url.isEmpty() && !creds.user.isEmpty() && !creds.password.isEmpty();
    if (!creds.valid) {
        creds.error = QStringLiteral("RPC URL, user, and password are required");
    }
    return creds;
}

QByteArray ConfigStore::buildMinerRuntimeJson(const WorkerProfile& profile, const RpcCredentials& creds)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("algo"), QStringLiteral("adam"));
    obj.insert(QStringLiteral("url"), creds.url);
    obj.insert(QStringLiteral("userpass"), creds.user + QLatin1Char(':') + creds.password);
    if (!profile.coinbaseAddress.isEmpty()) {
        obj.insert(QStringLiteral("coinbase-addr"), profile.coinbaseAddress);
    }
    obj.insert(QStringLiteral("no-color"), true);

    if (profile.engine == MinerEngine::CpuMiner) {
        obj.insert(QStringLiteral("threads"), qMax(1, profile.threads));
        if (profile.maxTemp > 0) {
            obj.insert(QStringLiteral("max-temp"), profile.maxTemp);
        }
        // cpuminer-opt enables HTTP/Telnet API only when -b is passed; omit when apiPort == 0.
        if (profile.apiPort > 0) {
            obj.insert(QStringLiteral("api-bind"), QStringLiteral("127.0.0.1:%1").arg(profile.apiPort));
        }
    } else {
        if (!profile.gpuDevices.trimmed().isEmpty()) {
            obj.insert(QStringLiteral("devices"), profile.gpuDevices.trimmed());
        }
        if (profile.gpuIntensity > 0.0) {
            obj.insert(QStringLiteral("intensity"), profile.gpuIntensity);
        }
        // ccminer defaults opt_api_port=4068 unless explicitly set to "0".
        obj.insert(QStringLiteral("api-bind"),
                   profile.apiPort > 0
                       ? QStringLiteral("127.0.0.1:%1").arg(profile.apiPort)
                       : QStringLiteral("0"));
    }

    return QJsonDocument(obj).toJson(QJsonDocument::Indented);
}

QString ConfigStore::sanitizeLogLine(const QString& rawLine)
{
    QString line = rawLine;
    // Strip ANSI escape sequences
    static const QRegularExpression reAnsi(QStringLiteral("\x1b\\[[0-9;]*[A-Za-z]"));
    line.remove(reAnsi);

    // Mask --userpass=USER:PASS or -O USER:PASS or -OUSER:PASS
    static const QRegularExpression reUserpassFlag(QStringLiteral("(--userpass(?:=|\\s+)|-O\\s*)([^\\s'\"]+)"));
    line.replace(reUserpassFlag, QStringLiteral("\\1***:***"));

    // Mask JSON "userpass": "USER:PASS"
    static const QRegularExpression reJsonPass(QStringLiteral("(\"userpass\"\\s*:\\s*\")([^\"]*)(\")"));
    line.replace(reJsonPass, QStringLiteral("\\1***:***\\3"));

    // Mask http(s)://user:pass@host URLs
    static const QRegularExpression reUrlAuth(QStringLiteral("(https?://)([^/\\s:@]+):([^/\\s@]+)@"));
    line.replace(reUrlAuth, QStringLiteral("\\1***:***@"));

    // Mask --rpcpassword=... / --rpcuser=...
    static const QRegularExpression reRpcFlag(QStringLiteral("(--rpc(?:password|user)(?:=|\\s+))([^\\s'\"]+)"));
    line.replace(reRpcFlag, QStringLiteral("\\1***"));

    return line.trimmed();
}
