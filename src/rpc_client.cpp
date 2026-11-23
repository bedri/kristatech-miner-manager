#include "rpc_client.h"

#include "config_store.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

#include <memory>

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

QString shellQuoteRpcArg(const QString& s)
{
    QString esc = s;
    esc.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + esc + QLatin1Char('\'');
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

void RpcClient::setTargetStatus(const QString& targetKey, const NodeMiningStatus& status)
{
    m_targetStatuses.insert(targetKey, status);
    Q_EMIT targetStatusUpdated(targetKey, status);
}

void RpcClient::setConsensusStatus(const ConsensusStatus& status)
{
    m_consensus = status;
    Q_EMIT consensusStatusUpdated(m_consensus);
}

void RpcClient::setPoolSeatsForTest(const QHash<QString, PoolSeatInfo>& seats, qint64 chainHeight)
{
    m_poolSeats = seats;
    m_lastPoolHeight = chainHeight;
    m_lastPoolFetchMs = QDateTime::currentMSecsSinceEpoch();
    for (auto it = m_targetStatuses.begin(); it != m_targetStatuses.end(); ++it) {
        it->poolSize = static_cast<int>(m_poolSeats.size());
    }
    Q_EMIT poolSeatsUpdated();
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
            setTargetStatus(tkey, st);
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
                          setTargetStatus(tkey, st);
                          return;
                      }
                      const QJsonObject obj = res.toObject();
                      st.reachable = true;
                      st.error.clear();
                      st.blockHeight = static_cast<qint64>(obj.value(QStringLiteral("blocks")).toDouble(0));
                      st.networkHashPs = obj.value(QStringLiteral("networkhashps")).toDouble(0.0);
                      st.difficulty = obj.value(QStringLiteral("difficulty")).toDouble(0.0);
                      st.chain = obj.value(QStringLiteral("chain")).toString().trimmed().toLower();
                      st.nodeErrors = obj.value(QStringLiteral("errors")).toString().trimmed();
                      st.internalGenerate = obj.value(QStringLiteral("generate")).toBool(false);
                      st.internalThreads = obj.value(QStringLiteral("genproclimit")).toInt(0);
                      st.poolSize = static_cast<int>(m_poolSeats.size());
                      setTargetStatus(tkey, st);

                      if (p.targetType != TargetType::RemoteSsh || m_poolSeats.isEmpty()) {
                          fetchPoolIfNeeded(p, creds, st.blockHeight);
                      }
                      fetchWalletStatusIfNeeded(p, creds, tkey);
                      if (p.targetType != TargetType::RemoteSsh || !m_consensus.known) {
                          fetchConsensusIfNeeded(p, creds, tkey, st.blockHeight);
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
                      it->poolSize = static_cast<int>(m_poolSeats.size());
                  }

                  Q_EMIT poolSeatsUpdated();
                  if (profile.targetType == TargetType::LocalWallet && creds.valid && !allAddrs.isEmpty()) {
                      probeLocalSeatOwnership(profile, creds, allAddrs);
                  }
              });
}

void RpcClient::fetchWalletStatusIfNeeded(const WorkerProfile& profile,
                                          const RpcCredentials& creds,
                                          const QString& targetKey)
{
    if (m_inflightWalletTargets.contains(targetKey)) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 throttleMs = 0;
    if (profile.targetType == TargetType::PodmanContainer) {
        throttleMs = 24000;
    } else if (profile.targetType == TargetType::RemoteSsh) {
        throttleMs = 60000;
    }
    const qint64 lastMs = m_lastWalletCheckMs.value(targetKey, 0);
    if (lastMs > 0 && throttleMs > 0 && (now - lastMs) < throttleMs) {
        return;
    }

    m_inflightWalletTargets.insert(targetKey);
    invokeRpc(profile, creds, QStringLiteral("getwalletinfo"), QJsonArray{},
              [this, targetKey](bool ok, const QJsonValue& res, const QString&) {
                  m_inflightWalletTargets.remove(targetKey);
                  m_lastWalletCheckMs.insert(targetKey, QDateTime::currentMSecsSinceEpoch());
                  if (!ok || !res.isObject()) {
                      // Non-fatal: pruned/walletless nodes or test mocks without getwalletinfo remain unlocked.
                      return;
                  }
                  const QJsonObject wobj = res.toObject();
                  NodeMiningStatus st = m_targetStatuses.value(targetKey);
                  st.walletChecked = true;
                  if (wobj.contains(QStringLiteral("unlocked_until"))) {
                      st.walletEncrypted = true;
                      st.walletUnlockedUntil = static_cast<qint64>(wobj.value(QStringLiteral("unlocked_until")).toDouble(0));
                      st.walletLocked = (st.walletUnlockedUntil == 0);
                  } else {
                      st.walletEncrypted = false;
                      st.walletUnlockedUntil = 0;
                      st.walletLocked = false;
                  }
                  setTargetStatus(targetKey, st);
              });
}

void RpcClient::fetchConsensusIfNeeded(const WorkerProfile& profile,
                                       const RpcCredentials& creds,
                                       const QString& targetKey,
                                       qint64 height)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_consensusFetchInflight) {
        return;
    }
    if (m_lastConsensusFetchMs > 0 && height == m_lastConsensusHeight && (now - m_lastConsensusFetchMs) < 30000) {
        return;
    }

    m_consensusFetchInflight = true;
    invokeRpc(profile, creds, QStringLiteral("getblockchaininfo"), QJsonArray{},
              [this, targetKey, height](bool ok, const QJsonValue& res, const QString&) {
                  m_consensusFetchInflight = false;
                  const qint64 doneMs = QDateTime::currentMSecsSinceEpoch();
                  m_lastConsensusHeight = height;
                  m_lastConsensusFetchMs = doneMs;
                  if (!ok || !res.isObject()) {
                      return;
                  }

                  const QJsonObject obj = res.toObject();
                  ConsensusStatus next;
                  next.known = true;
                  next.chain = obj.value(QStringLiteral("chain")).toString().trimmed().toLower();
                  next.initialBlockDownload = obj.value(QStringLiteral("initialblockdownload")).toBool(false);

                  const QJsonObject upgrades = obj.value(QStringLiteral("upgrades")).toObject();
                  for (auto it = upgrades.constBegin(); it != upgrades.constEnd(); ++it) {
                      const QString name = it.key().trimmed();
                      if (!it.value().isObject()) continue;
                      const QJsonObject uo = it.value().toObject();
                      if (uo.value(QStringLiteral("status")).toString().compare(QStringLiteral("active"), Qt::CaseInsensitive) == 0) {
                          next.activeUpgrades.append(name);
                          const QString low = name.toLower();
                          if (low == QStringLiteral("fairness")) next.fairnessActive = true;
                          else if (low == QStringLiteral("model d")) next.modelDActive = true;
                          else if (low == QStringLiteral("adam open entry")) next.openEntryActive = true;
                          else if (low == QStringLiteral("pombl")) next.pomblActive = true;
                      }
                  }

                  if (m_targetStatuses.contains(targetKey)) {
                      NodeMiningStatus st = m_targetStatuses.value(targetKey);
                      if (st.initialBlockDownload != next.initialBlockDownload) {
                          st.initialBlockDownload = next.initialBlockDownload;
                          setTargetStatus(targetKey, st);
                      }
                  }

                  setConsensusStatus(next);
              });
}

void RpcClient::probeLocalSeatOwnership(const WorkerProfile& profile, const RpcCredentials& creds, const QStringList& candidateAddrs)
{
    auto remaining = std::make_shared<int>(static_cast<int>(candidateAddrs.size()));
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
    const RpcCredentials creds = (profile.targetType == TargetType::PodmanContainer || profile.targetType == TargetType::RemoteSsh)
        ? RpcCredentials{}
        : ConfigStore::resolveCredentials(profile);
    if (!creds.valid && profile.targetType != TargetType::PodmanContainer && profile.targetType != TargetType::RemoteSsh) {
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
                      st.checked = true;
                      st.reachable = true;
                      st.internalGenerate = enable;
                      setTargetStatus(tkey, st);
                  }
                  if (cb) cb(ok, err);
              });
}

void RpcClient::renewMinerSeat(const WorkerProfile& profile,
                               const QString& track,
                               const QString& address,
                               qint64 tipHeight,
                               std::function<void(bool ok, const QString& txidOrErr)> cb)
{
    const QString addr = address.trimmed();
    const QString trk = track.trimmed();
    if (addr.isEmpty()) {
        if (cb) cb(false, QStringLiteral("Coinbase address is empty"));
        return;
    }
    if (trk != QStringLiteral("coin_lock")
        && trk != QStringLiteral("pow_lock")
        && trk != QStringLiteral("zerocoin_pow")) {
        if (cb) cb(false, QStringLiteral("Seat track '%1' is not renewable via registerminer").arg(trk));
        return;
    }

    const RpcCredentials creds = (profile.targetType == TargetType::LocalWallet || profile.targetType == TargetType::CustomRpc)
        ? ConfigStore::resolveCredentials(profile)
        : RpcCredentials{};

    if ((profile.targetType == TargetType::LocalWallet || profile.targetType == TargetType::CustomRpc) && !creds.valid) {
        if (cb) cb(false, creds.error);
        return;
    }

    QJsonArray params;
    if (trk == QStringLiteral("coin_lock")) {
        if (tipHeight <= 0) {
            if (cb) cb(false, QStringLiteral("Chain tip height unavailable; wait for RPC sync"));
            return;
        }
        params.append(QStringLiteral("lock"));
        params.append(static_cast<double>(tipHeight + 2900));
        params.append(addr);
    } else {
        params.append(QStringLiteral("pow"));
        params.append(QStringLiteral(""));
        params.append(addr);
    }

    invokeRpc(profile, creds, QStringLiteral("registerminer"), params,
              [this, cb](bool ok, const QJsonValue& res, const QString& err) {
                  if (ok) {
                      m_lastPoolFetchMs = 0;
                  }
                  if (cb) {
                      const QString detail = ok
                          ? (res.isString() ? res.toString().trimmed() : res.toVariant().toString().trimmed())
                          : err;
                      cb(ok, detail);
                  }
              },
              120000);
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
                          auto rem = std::make_shared<int>(static_cast<int>(addrs.size()));
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
                          RpcCallback cb,
                          int timeoutMs)
{
    if (profile.targetType == TargetType::PodmanContainer) {
        const QString podName = profile.podmanName.trimmed();
        if (!isValidPodmanName(podName)) {
            if (cb) cb(false, QJsonValue(), QStringLiteral("Invalid Podman container name"));
            return;
        }
        QStringList args{
            QStringLiteral("exec"),
            podName,
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
        const QString sshTarget = profile.sshTarget.trimmed();
        if (!isValidSshTarget(sshTarget)) {
            if (cb) cb(false, QJsonValue(), QStringLiteral("Invalid SSH target"));
            return;
        }
        QStringList remoteCmd{QStringLiteral("kristatech-cli"), shellQuoteRpcArg(method)};
        for (const QJsonValue& pv : params) {
            if (pv.isBool()) remoteCmd.append(pv.toBool() ? QStringLiteral("true") : QStringLiteral("false"));
            else if (pv.isDouble()) remoteCmd.append(QString::number(static_cast<qint64>(pv.toDouble())));
            else remoteCmd.append(shellQuoteRpcArg(pv.toString()));
        }
        QStringList args{
            QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
            QStringLiteral("-o"), QStringLiteral("ConnectTimeout=6"),
            QStringLiteral("-o"), QStringLiteral("RequestTTY=no"),
            QStringLiteral("-o"), QStringLiteral("ClearAllForwardings=yes"),
            QStringLiteral("-p"), QString::number(profile.sshPort > 0 ? profile.sshPort : 22),
            QStringLiteral("--"),
            sshTarget,
            remoteCmd.join(QLatin1Char(' '))
        };
        invokeCliProcess(QStringLiteral("ssh"), args, cb);
        return;
    }

    invokeHttpJsonRpc(creds, method, params, cb, timeoutMs);
}

void RpcClient::invokeHttpJsonRpc(const RpcCredentials& creds,
                                  const QString& method,
                                  const QJsonArray& params,
                                  RpcCallback cb,
                                  int timeoutMs)
{
    if (!creds.valid) {
        if (cb) cb(false, QJsonValue(), creds.error);
        return;
    }

    QUrl url(creds.url);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(timeoutMs > 0 ? timeoutMs : 5000);
    const QByteArray auth = (creds.user + QLatin1Char(':') + creds.password).toUtf8().toBase64();
    req.setRawHeader("Authorization", "Basic " + auth);

    QJsonObject payload;
    payload.insert(QStringLiteral("jsonrpc"), QStringLiteral("1.0"));
    payload.insert(QStringLiteral("id"), QStringLiteral("kmm"));
    payload.insert(QStringLiteral("method"), method);
    payload.insert(QStringLiteral("params"), params);

    static constexpr qint64 kMaxRpcResponseBytes = 16 * 1024 * 1024;
    auto bodyBuf = std::make_shared<QByteArray>();
    auto tooLarge = std::make_shared<bool>(false);

    QNetworkReply* reply = m_nam.post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::metaDataChanged, this, [reply, bodyBuf, tooLarge]() {
        if (*tooLarge) return;
        bool okLen = false;
        const qint64 contentLen = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong(&okLen);
        if (okLen && contentLen > kMaxRpcResponseBytes) {
            *tooLarge = true;
            bodyBuf->clear();
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::readyRead, this, [reply, bodyBuf, tooLarge]() {
        if (*tooLarge) return;
        const qint64 remaining = kMaxRpcResponseBytes - bodyBuf->size() + 1;
        const QByteArray chunk = reply->read(remaining);
        if (bodyBuf->size() + chunk.size() > kMaxRpcResponseBytes) {
            *tooLarge = true;
            bodyBuf->clear();
            reply->abort();
            return;
        }
        bodyBuf->append(chunk);
    });
    connect(reply, &QNetworkReply::finished, this, [reply, bodyBuf, tooLarge, cb]() {
        if (!*tooLarge && reply->bytesAvailable() > 0) {
            const qint64 remaining = kMaxRpcResponseBytes - bodyBuf->size() + 1;
            const QByteArray tail = reply->read(remaining);
            if (bodyBuf->size() + tail.size() > kMaxRpcResponseBytes) {
                *tooLarge = true;
                bodyBuf->clear();
            } else {
                bodyBuf->append(tail);
            }
        }
        const QByteArray body = *bodyBuf;
        bodyBuf->clear();
        const bool exceededCap = *tooLarge;
        const QNetworkReply::NetworkError netErr = reply->error();
        const QString errStr = reply->errorString();
        reply->deleteLater();

        if (exceededCap) {
            if (cb) cb(false, QJsonValue(), QStringLiteral("RPC response exceeded 16 MiB safety limit"));
            return;
        }
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
        if (!errVal.isNull() && !errVal.isUndefined()) {
            const QString msg = errVal.isObject()
                ? errVal.toObject().value(QStringLiteral("message")).toString()
                : errVal.toVariant().toString();
            if (cb) cb(false, QJsonValue(), msg.isEmpty() ? QStringLiteral("RPC error") : msg);
            return;
        }
        const QJsonValue resVal = obj.value(QStringLiteral("result"));
        if (netErr != QNetworkReply::NoError && (resVal.isNull() || resVal.isUndefined())) {
            if (cb) cb(false, QJsonValue(), errStr);
            return;
        }
        if (cb) cb(true, resVal, QString());
    });
}

void RpcClient::invokeCliProcess(const QString& program, const QStringList& args, RpcCallback cb)
{
    QString exe = QStandardPaths::findExecutable(program);
    if (exe.isEmpty()) {
        exe = program;
    }
    QProcess* proc = new QProcess(this);
    auto completed = std::make_shared<bool>(false);

    connect(proc, &QProcess::errorOccurred, this, [proc, program, cb, completed](QProcess::ProcessError err) {
        if (err != QProcess::FailedToStart || *completed) {
            return;
        }
        *completed = true;
        const QString msg = QStringLiteral("Failed to start %1: %2").arg(program, proc->errorString());
        proc->deleteLater();
        if (cb) cb(false, QJsonValue(), msg);
    });

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [proc, cb, completed](int exitCode, QProcess::ExitStatus exitStatus) {
                if (*completed) {
                    return;
                }
                *completed = true;
                const QByteArray out = proc->readAllStandardOutput();
                const QByteArray err = proc->readAllStandardError();
                proc->deleteLater();
                if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                    const QString errMsg = QString::fromUtf8(err).trimmed();
                    const QString outMsg = QString::fromUtf8(out).trimmed();
                    const QString detail = !errMsg.isEmpty() ? errMsg
                        : (!outMsg.isEmpty() ? outMsg : QStringLiteral("Exit code %1").arg(exitCode));
                    if (cb) cb(false, QJsonValue(), detail);
                    return;
                }
                QJsonValue val;
                parseCliJsonOutput(out, val);
                if (cb) cb(true, val, QString());
            });
    proc->start(exe, args);
}
