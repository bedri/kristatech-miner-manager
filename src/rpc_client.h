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
    void setTargetStatus(const QString& targetKey, const NodeMiningStatus& status);
    ConsensusStatus consensusStatus() const { return m_consensus; }
    void setConsensusStatus(const ConsensusStatus& status);
    PoolSeatInfo seatInfoForAddress(const QString& address) const;
    void setPoolSeatsForTest(const QHash<QString, PoolSeatInfo>& seats, qint64 chainHeight);
    QHash<QString, PoolSeatInfo> allKnownSeats() const { return m_poolSeats; }
    QVector<QPair<QString, PoolSeatInfo>> localOwnedSeats() const { return m_localOwnedSeats; }

    void setInternalGenerate(const WorkerProfile& profile,
                             bool enable,
                             std::function<void(bool ok, const QString& err)> cb);

    void renewMinerSeat(const WorkerProfile& profile,
                        const QString& track,
                        const QString& address,
                        qint64 tipHeight,
                        std::function<void(bool ok, const QString& txidOrErr)> cb);

    void discoverLocalRegisteredSeats(const QString& confPath,
                                      std::function<void(QVector<QString> addresses)> cb);

Q_SIGNALS:
    void targetStatusUpdated(const QString& targetKey, const NodeMiningStatus& status);
    void consensusStatusUpdated(const ConsensusStatus& status);
    void poolSeatsUpdated();

private:
    using RpcCallback = std::function<void(bool ok, const QJsonValue& result, const QString& err)>;

    void invokeRpc(const WorkerProfile& profile,
                   const RpcCredentials& creds,
                   const QString& method,
                   const QJsonArray& params,
                   RpcCallback cb,
                   int timeoutMs = 5000);

    void invokeHttpJsonRpc(const RpcCredentials& creds,
                           const QString& method,
                           const QJsonArray& params,
                           RpcCallback cb,
                           int timeoutMs = 5000);

    void invokeCliProcess(const QString& program,
                          const QStringList& args,
                          RpcCallback cb);

    void fetchPoolIfNeeded(const WorkerProfile& profile, const RpcCredentials& creds, qint64 height);
    void fetchWalletStatusIfNeeded(const WorkerProfile& profile, const RpcCredentials& creds, const QString& targetKey);
    void fetchConsensusIfNeeded(const WorkerProfile& profile, const RpcCredentials& creds, const QString& targetKey, qint64 height);
    void probeLocalSeatOwnership(const WorkerProfile& profile, const RpcCredentials& creds, const QStringList& candidateAddrs);

    QNetworkAccessManager m_nam;
    QHash<QString, NodeMiningStatus> m_targetStatuses;
    QSet<QString> m_inflightTargets;
    QSet<QString> m_inflightWalletTargets;
    QHash<QString, qint64> m_lastWalletCheckMs;
    ConsensusStatus m_consensus;
    qint64 m_lastConsensusHeight{-1};
    qint64 m_lastConsensusFetchMs{0};
    bool m_consensusFetchInflight{false};
    QHash<QString, PoolSeatInfo> m_poolSeats;
    QVector<QPair<QString, PoolSeatInfo>> m_localOwnedSeats;
    qint64 m_lastPoolHeight{-1};
    qint64 m_lastPoolFetchMs{0};
    bool m_poolFetchInflight{false};
};
