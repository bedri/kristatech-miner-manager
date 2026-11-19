#include "rpc_client.h"

#include "config_store.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QUrl>

namespace {

bool parseCliJsonOutput(const QByteArray& raw, QJsonValue& valOut)
{
    const QByteArray trimmed = raw.trimmed();
    if (trimmed.isEmpty()) {
        valOut = QJsonValue();
        return true;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed, &err);
    if (err.error == QJsonParseError::NoError) {
        if (doc.isArray()) valOut = doc.array();
        else if (doc.isObject()) valOut = doc.object();
        return true;
    }
    bool okInt = false;
    const qint64 num = trimmed.toLongLong(&okInt);
    if (okInt) {
        valOut = static_cast<double>(num);
        return true;
    }
    if (trimmed == "true") {
        valOut = true;
        return true;
    }
    if (trimmed == "false") {
        valOut = false;
        return true;
    }
    valOut = QString::fromUtf8(trimmed);
    return true;
}

} // namespace

RpcClient::RpcClient(QObject* parent)
    : QObject(parent)
{
}

RpcClient::~RpcClient()
{
    for (QProcess* p : findChildren<QProcess*>()) {
        p->disconnect(this);
        if (p->state() != QProcess::NotRunning) {
            p->kill();
            p->waitForFinished(300);
        }
    }
}

NodeMiningStatus RpcClient::statusForTarget(const QString& targetKey) const
{
    return m_targetStatuses.value(targetKey);
}

PoolSeatInfo RpcClient::seatInfoForAddress(const QString& address) const
{
    if (m_poolSeats.contains(address)) {
        return m_poolSeats.value(address);
    }
    PoolSeatInfo info;
    info.known = (m_lastPoolFetchMs > 0);
    info.registered = false;
    info.chainHeight = qMax<qint64>(0, m_lastPoolHeight);
    return info;
}

void RpcClient::refreshTargets(const QVector<WorkerProfile>& profiles)
{
    QSet<QString> seenKeys;
    for (const WorkerProfile& p : profiles) {
        const QString tkey = p.targetKey();
        if (seenKeys.contains(tkey) || m_inflightTargets.contains(tkey)) {
            continue;
        }
        seenKeys.insert(tkey);

        if (p.targetType == TargetType::RemoteSsh && p.sshTarget.trimmed().isEmpty()) {
            continue;
        }

        const RpcCredentials creds = (p.targetType == TargetType::PodmanContainer || p.targetType == TargetType::RemoteSsh)
            ? RpcCredentials{}
            : ConfigStore::resolveCredentials(p);

        if ((p.targetType == TargetType::LocalWallet || p.targetType == TargetType::CustomRpc) && !creds.valid) {
            NodeMiningStatus st;
            st.checked = true;
            st.reachable = false;
            st.error = creds.error;
            m_targetStatuses.insert(tkey, st);
            Q_EMIT targetStatusUpdated(tkey, st);
            continue;
        }

        m_inflightTargets.insert(tkey);
        invokeRpc(p, creds, QStringLiteral("getmininginfo"), QJsonArray{},
                  [this, tkey, p, creds](bool ok, const QJsonValue& res, const QString& err) {
                      m_inflightTargets.remove(tkey);
                      NodeMiningStatus st = m_targetStatuses.value(tkey);
                      st.checked = true;
                      if (!ok || !res.isObject()) {
                          st.reachable = false;
                          st.error = err.isEmpty() ? QStringLiteral("RPC unreachable") : err;
                          m_targetStatuses.insert(tkey, st);
                          Q_EMIT targetStatusUpdated(tkey, st);
                          return;
                      }
                      const QJsonObject obj = res.toObject();
                      st.reachable = true;
                      st.error.clear();
                      st.blockHeight = static_cast<qint64>(obj.value(QStringLiteral("blocks")).toDouble(0));
                      st.networkHashPs = obj.value(QStringLiteral("networkhashps")).toDouble(0.0);
                      st.internalGenerate = obj.value(QStringLiteral("generate")).toBool(false);
                      st.internalThreads = obj.value(QStringLiteral("genproclimit")).toInt(0);
                      st.poolSize = m_poolSeats.size();
                      m_targetStatuses.insert(tkey, st);
                      Q_EMIT targetStatusUpdated(tkey, st);

                      if (p.targetType != TargetType::RemoteSsh) {
                          fetchPoolIfNeeded(p, creds, st.blockHeight);
                      }
                  });
    }
}

void RpcClient::fetchPoolIfNeeded(const WorkerProfile& profile, const RpcCredentials& creds, qint64 height)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_poolFetchInflight) {
        return;
    }
    if (height == m_lastPoolHeight && (now - m_lastPoolFetchMs) < 12000 && !m_poolSeats.isEmpty()) {
        return;
    }

    m_poolFetchInflight = true;
    invokeRpc(profile, creds, QStringLiteral("getadamminers"), QJsonArray{},
              [this, height, profile, creds](bool ok, const QJsonValue& res, const QString&) {
                  m_poolFetchInflight = false;
                  if (!ok || !res.isArray()) {
                      return;
                  }
                  const QJsonArray arr = res.toArray();
                  QHash<QString, PoolSeatInfo> next;
                  QStringList allAddrs;
                  next.reserve(arr.size());
                  for (const QJsonValue& v : arr) {
                      if (!v.isObject()) continue;
                      const QJsonObject o = v.toObject();
                      const QString addr = o.value(QStringLiteral("address")).toString().trimmed();
                      if (addr.isEmpty()) continue;
                      PoolSeatInfo seat;
                      seat.known = true;
                      seat.registered = true;
                      seat.track = o.value(QStringLiteral("track")).toString();
                      seat.unlockHeight = static_cast<qint64>(o.value(QStringLiteral("unlock_height")).toDouble(0));
                      seat.chainHeight = height;
                      seat.remainingBlocks = (seat.unlockHeight > height) ? (seat.unlockHeight - height) : 0;
                      next.insert(addr, seat);
                      allAddrs.append(addr);
                  }
                  m_poolSeats = next;
                  m_lastPoolHeight = height;
                  m_lastPoolFetchMs = QDateTime::currentMSecsSinceEpoch();

                  for (auto it = m_targetStatuses.begin(); it != m_targetStatuses.end(); ++it) {
                      it->poolSize = m_poolSeats.size();
                  }

                  Q_EMIT poolSeatsUpdated();
                  if (profile.targetType == TargetType::LocalWallet && creds.valid && !allAddrs.isEmpty()) {
                      probeLocalSeatOwnership(profile, creds, allAddrs);
                  }
              });
}

void RpcClient::probeLocalSeatOwnership(const WorkerProfile& profile, const RpcCredentials& creds, const QStringList& candidateAddrs)
{
    auto remaining = std::make_shared<int>(candidateAddrs.size());
    auto owned = std::make_shared<QVector<QPair<QString, PoolSeatInfo>>>();

    for (const QString& addr : candidateAddrs) {
        QJsonArray params;
        params.append(addr);
        invokeHttpJsonRpc(creds, QStringLiteral("validateaddress"), params,
                          [this, addr, remaining, owned](bool ok, const QJsonValue& res, const QString&) {
                              if (ok && res.isObject() && res.toObject().value(QStringLiteral("ismine")).toBool(false)) {
                                  owned->append({addr, seatInfoForAddress(addr)});
                              }
                              --(*remaining);
                              if (*remaining <= 0) {
                                  m_localOwnedSeats = *owned;
                                  Q_EMIT poolSeatsUpdated();
                              }
                          });
    }
    Q_UNUSED(profile);
}

void RpcClient::refreshPoolAddressesFromTarget(const WorkerProfile& profile)
{
    const RpcCredentials creds = ConfigStore::resolveCredentials(profile);
    if (!creds.valid && profile.targetType != TargetType::PodmanContainer) {
        return;
    }
    fetchPoolIfNeeded(profile, creds, qMax<qint64>(0, m_lastPoolHeight));
}

void RpcClient::setInternalGenerate(const WorkerProfile& profile,
                                    bool enable,
                                    std::function<void(bool ok, const QString& err)> cb)
{
    const RpcCredentials creds = (profile.targetType == TargetType::LocalWallet || profile.targetType == TargetType::CustomRpc)
        ? ConfigStore::resolveCredentials(profile)
        : RpcCredentials{};

    if ((profile.targetType == TargetType::LocalWallet || profile.targetType == TargetType::CustomRpc) && !creds.valid) {
        if (cb) cb(false, creds.error);
        return;
    }

    QJsonArray params;
    params.append(enable);
    invokeRpc(profile, creds, QStringLiteral("setgenerate"), params,
              [this, profile, enable, cb](bool ok, const QJsonValue&, const QString& err) {
                  if (ok) {
                      const QString tkey = profile.targetKey();
                      NodeMiningStatus st = m_targetStatuses.value(tkey);
                      st.internalGenerate = enable;
                      m_targetStatuses.insert(tkey, st);
                      Q_EMIT targetStatusUpdated(tkey, st);
                  }
                  if (cb) cb(ok, err);
              });
}

void RpcClient::discoverLocalRegisteredSeats(const QString& confPath, std::function<void(QVector<QString> addresses)> cb)
{
    WorkerProfile p;
    p.targetType = TargetType::LocalWallet;
    p.confPath = confPath;
    const RpcCredentials creds = ConfigStore::resolveCredentials(p);
    if (!creds.valid) {
        if (cb) cb({});
        return;
    }
    invokeHttpJsonRpc(creds, QStringLiteral("getadamminers"), QJsonArray{},
                      [this, creds, cb](bool ok, const QJsonValue& res, const QString&) {
                          if (!ok || !res.isArray()) {
                              if (cb) cb({});
                              return;
                          }
                          QStringList addrs;
                          for (const QJsonValue& v : res.toArray()) {
                              const QString a = v.toObject().value(QStringLiteral("address")).toString().trimmed();
                              if (!a.isEmpty()) addrs.append(a);
                          }
                          if (addrs.isEmpty()) {
                              if (cb) cb({});
                              return;
                          }
                          auto rem = std::make_shared<int>(addrs.size());
                          auto owned = std::make_shared<QVector<QString>>();
                          for (const QString& a : addrs) {
                              QJsonArray vp;
                              vp.append(a);
                              invokeHttpJsonRpc(creds, QStringLiteral("validateaddress"), vp,
                                                [a, rem, owned, cb](bool okv, const QJsonValue& rv, const QString&) {
                                                    if (okv && rv.isObject() && rv.toObject().value(QStringLiteral("ismine")).toBool(false)) {
                                                        owned->append(a);
                                                    }
                                                    --(*rem);
                                                    if (*rem <= 0 && cb) {
                                                        cb(*owned);
                                                    }
                                                });
                          }
                      });
}

void RpcClient::invokeRpc(const WorkerProfile& profile,
                          const RpcCredentials& creds,
                          const QString& method,
                          const QJsonArray& params,
                          RpcCallback cb)
{
    if (profile.targetType == TargetType::PodmanContainer) {
        QStringList args{
            QStringLiteral("exec"),
            profile.podmanName.trimmed(),
            QStringLiteral("/hostbin/kristatech-cli"),
            QStringLiteral("-datadir=/data"),
            method
        };
        for (const QJsonValue& pv : params) {
            if (pv.isBool()) args.append(pv.toBool() ? QStringLiteral("true") : QStringLiteral("false"));
            else if (pv.isDouble()) args.append(QString::number(static_cast<qint64>(pv.toDouble())));
            else args.append(pv.toString());
        }
        invokeCliProcess(QStringLiteral("podman"), args, cb);
        return;
    }

    if (profile.targetType == TargetType::RemoteSsh) {
        QStringList remoteCmd{QStringLiteral("kristatech-cli"), method};
        for (const QJsonValue& pv : params) {
            if (pv.isBool()) remoteCmd.append(pv.toBool() ? QStringLiteral("true") : QStringLiteral("false"));
            else if (pv.isDouble()) remoteCmd.append(QString::number(static_cast<qint64>(pv.toDouble())));
            else remoteCmd.append(pv.toString());
        }
        QStringList args{
            QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
            QStringLiteral("-o"), QStringLiteral("ConnectTimeout=6"),
            QStringLiteral("-p"), QString::number(profile.sshPort > 0 ? profile.sshPort : 22),
            profile.sshTarget.trimmed(),
            remoteCmd.join(QLatin1Char(' '))
        };
        invokeCliProcess(QStringLiteral("ssh"), args, cb);
        return;
    }

    invokeHttpJsonRpc(creds, method, params, cb);
}

void RpcClient::invokeHttpJsonRpc(const RpcCredentials& creds,
                                  const QString& method,
                                  const QJsonArray& params,
                                  RpcCallback cb)
{
    if (!creds.valid) {
        if (cb) cb(false, QJsonValue(), creds.error);
        return;
    }

    QUrl url(creds.url);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(5000);
    const QByteArray auth = (creds.user + QLatin1Char(':') + creds.password).toUtf8().toBase64();
    req.setRawHeader("Authorization", "Basic " + auth);

    QJsonObject payload;
    payload.insert(QStringLiteral("jsonrpc"), QStringLiteral("1.0"));
    payload.insert(QStringLiteral("id"), QStringLiteral("kmm"));
    payload.insert(QStringLiteral("method"), method);
    payload.insert(QStringLiteral("params"), params);

    QNetworkReply* reply = m_nam.post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        const QByteArray body = reply->readAll();
        const QNetworkReply::NetworkError netErr = reply->error();
        const QString errStr = reply->errorString();
        reply->deleteLater();

        if (netErr != QNetworkReply::NoError && body.isEmpty()) {
            if (cb) cb(false, QJsonValue(), errStr);
            return;
        }
        QJsonParseError jerr{};
        const QJsonDocument doc = QJsonDocument::fromJson(body, &jerr);
        if (jerr.error != QJsonParseError::NoError || !doc.isObject()) {
            if (cb) cb(false, QJsonValue(), errStr.isEmpty() ? jerr.errorString() : errStr);
            return;
        }
        const QJsonObject obj = doc.object();
        const QJsonValue errVal = obj.value(QStringLiteral("error"));
        if (!errVal.isNull() && errVal.isObject()) {
            const QString msg = errVal.toObject().value(QStringLiteral("message")).toString();
            if (cb) cb(false, QJsonValue(), msg.isEmpty() ? QStringLiteral("RPC error") : msg);
            return;
        }
        if (cb) cb(true, obj.value(QStringLiteral("result")), QString());
    });
}

void RpcClient::invokeCliProcess(const QString& program, const QStringList& args, RpcCallback cb)
{
    QProcess* proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [proc, cb](int exitCode, QProcess::ExitStatus) {
                const QByteArray out = proc->readAllStandardOutput();
                const QByteArray err = proc->readAllStandardError();
                proc->deleteLater();
                if (exitCode != 0) {
                    if (cb) cb(false, QJsonValue(), QString::fromUtf8(err).trimmed());
                    return;
                }
                QJsonValue val;
                parseCliJsonOutput(out, val);
                if (cb) cb(true, val, QString());
            });
    proc->start(program, args);
}
