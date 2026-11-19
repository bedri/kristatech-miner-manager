#pragma once

#include "config_store.h"
#include "models.h"
#include "process_scanner.h"
#include "rpc_client.h"

#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVector>

class QProcess;

/**
 * Owns worker profiles, runtime telemetry, /proc adoption,
 * SSH worker orchestration, log tailing/parsing, and crash auto-restart.
 */
class WorkerController : public QObject
{
    Q_OBJECT

public:
    explicit WorkerController(ConfigStore* store, RpcClient* rpc, QObject* parent = nullptr);
    ~WorkerController() override;

    const QVector<WorkerProfile>& profiles() const { return m_profiles; }
    WorkerProfile profileById(const QString& id) const;
    WorkerRuntime runtimeById(const QString& id) const;

    void loadInitialState(bool autoDiscoverIfEmpty = true);
    bool saveProfiles(QString* err = nullptr);

    QString addProfile(WorkerProfile profile);
    bool updateProfile(const WorkerProfile& profile);
    bool removeProfile(const QString& id, bool stopIfRunning = true);

    void startWorker(const QString& id);
    void stopWorker(const QString& id);
    void restartWorker(const QString& id);

    void startAll();
    void stopAll(bool includeRemoteSsh = false);
    int countRunningLocalOrPodman() const;
    int countRunningUnregisteredOrDeferred() const;
    int stopUnregisteredOrDeferred();

    bool autoStopOnDeferred() const { return m_autoStopOnDeferred; }
    void setAutoStopOnDeferred(bool enabled);

    void clearWorkerLog(const QString& id);

    /** Scans live /proc processes and adopts matching workers without restarting them. */
    int adoptRunningProcesses(bool createProfilesForUnknown = true);

    /** Discovers live miners, local wallet pool seats, and Podman containers. */
    void autoDiscoverFleet(std::function<void(int added, int adopted)> cb = nullptr);

    void importProfiles(const QVector<WorkerProfile>& imported, int* addedOut = nullptr);

    /** Shutdown helpers used when quitting GUI. */
    void stopAllManagedAndCleanup();
    void detachAll();

    /** Pure log parser helper (unit-tested). */
    static void parseLogLine(const QString& sanitizedLine, WorkerRuntime& rt);

    /** Splits sanitized CLI flags respecting simple quotes. */
    static QStringList splitExtraArgs(const QString& raw);

Q_SIGNALS:
    void workersChanged();
    void workerLogAppended(const QString& id, const QStringList& newLines);
    void discoveryFinished(int addedCount, int adoptedCount);
    void autoStopOnDeferredChanged(bool enabled);
    void workerAutoStopped(const QString& id, const QString& reason);

private Q_SLOTS:
    void onLogPollTick();
    void onHealthCheckTick();

private:
    int indexOfId(const QString& id) const;
    QString makeUniqueId(const QString& base) const;
    bool matchesProcess(const WorkerProfile& p, const DiscoveredProcess& dp) const;
    bool isWorkerRunning(const WorkerProfile& p, const WorkerRuntime& rt) const;
    void autoStopIfDeferred(const QString& id);

    void startLocalOrPodman(const WorkerProfile& p, WorkerRuntime& rt);
    void launchLocalOrPodmanWithPid(const WorkerProfile& p, WorkerRuntime& rt, const QString& exePath, qint64 podPid);
    void startRemoteSsh(const WorkerProfile& p, WorkerRuntime& rt);
    void stopRemoteSsh(const WorkerProfile& p, WorkerRuntime& rt);
    void attachSshLogTail(const WorkerProfile& p);
    void detachSshLogTail(const QString& id);
    void readLogIncremental(const QString& id, WorkerRuntime& rt, qint64 tailWindowBytes = 0);
    void appendSanitizedLines(const QString& id, WorkerRuntime& rt, const QStringList& lines);

    ConfigStore* m_store{nullptr};
    RpcClient* m_rpc{nullptr};
    QVector<WorkerProfile> m_profiles;
    QHash<QString, WorkerRuntime> m_runtimes;
    QHash<QString, QProcess*> m_sshTailProcs;
    QHash<QString, QProcess*> m_podmanStartProcs;
    QTimer m_logTimer;
    QTimer m_healthTimer;
    int m_healthTickCounter{0};
    bool m_autoStopOnDeferred{false};
};
