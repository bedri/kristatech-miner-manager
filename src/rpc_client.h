#pragma once

#include "models.h"

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QVector>

#include <functional>

/**
 * Asynchronous JSON-RPC / CLI client for querying Local, Podman, Custom RPC,
 * and SSH KristaTech nodes without blocking the UI thread.
 */
class RpcClient : public QObject
{
    Q_OBJECT

public:
    explicit RpcClient(QObject* parent = nullptr);
    ~RpcClient() override;

    void refreshTargets(const QVector<WorkerProfile>& profiles);
    void refreshPoolAddressesFromTarget(const WorkerProfile& profile);

    NodeMiningStatus statusForTarget(const QString& targetKey) const;
    PoolSeatInfo seatInfoForAddress(const QString& address) const;
    QHash<QString, PoolSeatInfo> allKnownSeats() const { return m_poolSeats; }
    QVector<QPair<QString, PoolSeatInfo>> localOwnedSeats() const { return m_localOwnedSeats; }

    void setInternalGenerate(const WorkerProfile& profile,
                             bool enable,
                             std::function<void(bool ok, const QString& err)> cb);

    void discoverLocalRegisteredSeats(const QString& confPath,
                                      std::function<void(QVector<QString> addresses)> cb);

Q_SIGNALS:
    void targetStatusUpdated(const QString& targetKey, const NodeMiningStatus& status);
    void poolSeatsUpdated();

private:
    using RpcCallback = std::function<void(bool ok, const QJsonValue& result, const QString& err)>;

    void invokeRpc(const WorkerProfile& profile,
                   const RpcCredentials& creds,
                   const QString& method,
                   const QJsonArray& params,
                   RpcCallback cb);

    void invokeHttpJsonRpc(const RpcCredentials& creds,
                           const QString& method,
                           const QJsonArray& params,
                           RpcCallback cb);

    void invokeCliProcess(const QString& program,
                          const QStringList& args,
                          RpcCallback cb);

    void fetchPoolIfNeeded(const WorkerProfile& profile, const RpcCredentials& creds, qint64 height);
    void probeLocalSeatOwnership(const WorkerProfile& profile, const RpcCredentials& creds, const QStringList& candidateAddrs);

    QNetworkAccessManager m_nam;
    QHash<QString, NodeMiningStatus> m_targetStatuses;
    QSet<QString> m_inflightTargets;
    QHash<QString, PoolSeatInfo> m_poolSeats;
    QVector<QPair<QString, PoolSeatInfo>> m_localOwnedSeats;
    qint64 m_lastPoolHeight{-1};
    qint64 m_lastPoolFetchMs{0};
    bool m_poolFetchInflight{false};
};
