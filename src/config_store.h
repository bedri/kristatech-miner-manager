#pragma once

#include "models.h"

#include <QByteArray>
#include <QString>
#include <QVector>

/**
 * Handles secure profile persistence, .conf credential parsing,
 * 0600 runtime miner JSON generation, and log credential sanitization.
 */
class ConfigStore
{
public:
    explicit ConfigStore(const QString& baseDir = QString());

    static QString defaultConfigDir();

    QString baseDir() const { return m_baseDir; }
    QString profilesFilePath() const;
    QString runtimeDirPath() const;
    QString logsDirPath() const;
    QString runtimeJsonPath(const QString& workerId) const;
    QString defaultLogPath(const QString& workerId) const;

    bool ensureDirectories() const;

    QVector<WorkerProfile> loadProfiles(QString* errorOut = nullptr) const;
    bool saveProfiles(const QVector<WorkerProfile>& profiles, QString* errorOut = nullptr) const;

    QByteArray exportProfilesJson(const QVector<WorkerProfile>& profiles, bool redactSecrets = true) const;
    QVector<WorkerProfile> parseImportedProfilesJson(const QByteArray& data, QString* errorOut = nullptr) const;

    /** Resolves credentials live for LocalWallet/PodmanContainer or from profile for CustomRpc. */
    static RpcCredentials resolveCredentials(const WorkerProfile& profile);

    /** Parses rpcport, rpcuser, rpcpassword, testnet from a kristatech.conf body. */
    static RpcCredentials parseKristaTechConf(const QString& confContent, int defaultPort = 27998);

    /** Builds JSON bytes compatible with both cpuminer-opt and ccminer (-c FILE). */
    static QByteArray buildMinerRuntimeJson(const WorkerProfile& profile, const RpcCredentials& creds);

    /** Masks CLI/JSON/URL credentials and strips ANSI escape sequences from log lines. */
    static QString sanitizeLogLine(const QString& rawLine);

    /** Atomically writes file with 0600 owner-only permissions. */
    static bool writeFile0600(const QString& path, const QByteArray& content, QString* errorOut = nullptr);

private:
    QString m_baseDir;
};
