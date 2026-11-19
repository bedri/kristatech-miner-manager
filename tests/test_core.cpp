#include "config_store.h"
#include "models.h"
#include "process_scanner.h"
#include "worker_controller.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

class TestCore : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testParseKristaTechConf();
    void testSanitizeLogLine();
    void testBuildMinerRuntimeJson();
    void testProfileRoundtripAndPermissions();
    void testParseLogLine();
    void testParseCmdlineAndSplitArgs();
};

void TestCore::testParseKristaTechConf()
{
    const QString conf = QStringLiteral(
        "# KristaTech Mainnet\n"
        "server=1\n"
        "rpcuser=kristarpc\n"
        "rpcpassword=secret=with=equals\n"
        "rpcport=27979\n");

    const RpcCredentials c = ConfigStore::parseKristaTechConf(conf, 27998);
    QVERIFY(c.valid);
    QCOMPARE(c.user, QStringLiteral("kristarpc"));
    QCOMPARE(c.password, QStringLiteral("secret=with=equals"));
    QCOMPARE(c.url, QStringLiteral("http://127.0.0.1:27979/"));

    const QString testConf = QStringLiteral("testnet=1\nrpcuser=u\nrpcpassword=p\n");
    const RpcCredentials tc = ConfigStore::parseKristaTechConf(testConf, 27998);
    QVERIFY(tc.valid);
    QCOMPARE(tc.url, QStringLiteral("http://127.0.0.1:28998/"));
}

void TestCore::testSanitizeLogLine()
{
    const QString raw1 = QStringLiteral("\x1b[32mStarting --userpass=myuser:mysupersecret -O u2:p2 http://bob:pw123@127.0.0.1:27998/\x1b[0m");
    const QString s1 = ConfigStore::sanitizeLogLine(raw1);
    QVERIFY(!s1.contains(QStringLiteral("mysupersecret")));
    QVERIFY(!s1.contains(QStringLiteral("p2")));
    QVERIFY(!s1.contains(QStringLiteral("pw123")));
    QVERIFY(!s1.contains(QStringLiteral("\x1b[32m")));
    QVERIFY(s1.contains(QStringLiteral("***:***")));

    const QString raw2 = QStringLiteral("{\"userpass\": \"alice:hunter2\", \"algo\": \"adam\"}");
    const QString s2 = ConfigStore::sanitizeLogLine(raw2);
    QVERIFY(!s2.contains(QStringLiteral("hunter2")));
    QVERIFY(s2.contains(QStringLiteral("\"userpass\": \"***:***\"")));
}

void TestCore::testBuildMinerRuntimeJson()
{
    RpcCredentials creds;
    creds.url = QStringLiteral("http://127.0.0.1:27998/");
    creds.user = QStringLiteral("kristarpc");
    creds.password = QStringLiteral("p@ss");
    creds.valid = true;

    WorkerProfile cpu;
    cpu.engine = MinerEngine::CpuMiner;
    cpu.coinbaseAddress = QStringLiteral("KTdHzRF4e71xcU1ea8tePLmZNijUnH6ZNYm");
    cpu.threads = 4;
    cpu.maxTemp = 78;
    cpu.apiPort = 0;

    const QJsonObject cpuObj = QJsonDocument::fromJson(ConfigStore::buildMinerRuntimeJson(cpu, creds)).object();
    QCOMPARE(cpuObj.value(QStringLiteral("algo")).toString(), QStringLiteral("adam"));
    QCOMPARE(cpuObj.value(QStringLiteral("threads")).toInt(), 4);
    QCOMPARE(cpuObj.value(QStringLiteral("max-temp")).toInt(), 78);
    QCOMPARE(cpuObj.value(QStringLiteral("no-color")).toBool(), true);
    QVERIFY(!cpuObj.contains(QStringLiteral("api-bind")));

    cpu.apiPort = 4048;
    const QJsonObject cpuApiObj = QJsonDocument::fromJson(ConfigStore::buildMinerRuntimeJson(cpu, creds)).object();
    QCOMPARE(cpuApiObj.value(QStringLiteral("api-bind")).toString(), QStringLiteral("127.0.0.1:4048"));

    WorkerProfile gpu;
    gpu.engine = MinerEngine::CcMiner;
    gpu.coinbaseAddress = QStringLiteral("KTdHzRF4e71xcU1ea8tePLmZNijUnH6ZNYm");
    gpu.gpuDevices = QStringLiteral("0");
    gpu.gpuIntensity = 20.5;
    gpu.apiPort = 0;

    const QJsonObject gpuObj = QJsonDocument::fromJson(ConfigStore::buildMinerRuntimeJson(gpu, creds)).object();
    QCOMPARE(gpuObj.value(QStringLiteral("algo")).toString(), QStringLiteral("adam"));
    QCOMPARE(gpuObj.value(QStringLiteral("devices")).toString(), QStringLiteral("0"));
    QCOMPARE(gpuObj.value(QStringLiteral("intensity")).toDouble(), 20.5);
    QCOMPARE(gpuObj.value(QStringLiteral("api-bind")).toString(), QStringLiteral("0"));
}

void TestCore::testProfileRoundtripAndPermissions()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    ConfigStore store(tmpDir.path());

    WorkerProfile local;
    local.id = QStringLiteral("local_1");
    local.name = QStringLiteral("Local Worker");
    local.targetType = TargetType::LocalWallet;
    local.confPath = QStringLiteral("/home/user/.kristatech/kristatech.conf");
    local.rpcUser = QStringLiteral("should_be_omitted");
    local.rpcPassword = QStringLiteral("secret_must_never_write");
    local.coinbaseAddress = QStringLiteral("KTdHzRF4e71xcU1ea8tePLmZNijUnH6ZNYm");

    WorkerProfile ssh;
    ssh.id = QStringLiteral("ssh_1");
    ssh.name = QStringLiteral("Remote Rig");
    ssh.targetType = TargetType::RemoteSsh;
    ssh.sshTarget = QStringLiteral("user@example.com");
    ssh.rpcUrl = QStringLiteral("http://127.0.0.1:27998/");
    ssh.rpcUser = QStringLiteral("remuser");
    ssh.rpcPassword = QStringLiteral("rempass");

    QVERIFY(store.saveProfiles({local, ssh}));

    const QFileInfo fi(store.profilesFilePath());
    const QFileDevice::Permissions perms = fi.permissions();
    QVERIFY(perms.testFlag(QFileDevice::ReadOwner));
    QVERIFY(perms.testFlag(QFileDevice::WriteOwner));
    QVERIFY(!perms.testFlag(QFileDevice::ReadGroup));
    QVERIFY(!perms.testFlag(QFileDevice::ReadOther));

    QFile rawFile(store.profilesFilePath());
    QVERIFY(rawFile.open(QIODevice::ReadOnly));
    const QByteArray rawBytes = rawFile.readAll();
    QVERIFY(!rawBytes.contains("secret_must_never_write"));
    QVERIFY(!rawBytes.contains("should_be_omitted"));

    const QByteArray redacted = store.exportProfilesJson({local, ssh}, /*redactSecrets=*/true);
    QVERIFY(!redacted.contains("rempass"));

    const QVector<WorkerProfile> loaded = store.loadProfiles();
    QCOMPARE(loaded.size(), 2);
    QCOMPARE(loaded[0].id, QStringLiteral("local_1"));
    QVERIFY(loaded[0].rpcPassword.isEmpty());
    QCOMPARE(loaded[1].sshTarget, QStringLiteral("user@example.com"));
    QCOMPARE(loaded[1].rpcPassword, QStringLiteral("rempass"));
}

void TestCore::testParseLogLine()
{
    WorkerRuntime rt;
    rt.status = WorkerStatus::Starting;
    rt.userStopped = false;
    WorkerController::parseLogLine(QStringLiteral("[2026-11-19 11:27:33] New Block 4513, Tx 0, Net Diff 0.00024414"), rt);
    QCOMPARE(rt.lastBlock, 4513);
    QCOMPARE(rt.status, WorkerStatus::Solving);

    WorkerController::parseLogLine(QStringLiteral("Miner TTF @ 36.12 kh/s 29s, Net TTF @ 2672.26 kh/s 0s"), rt);
    QCOMPARE(rt.hashrateKhs, 36.12);
    QCOMPARE(rt.hashrateDisplay, QStringLiteral("36.12 kH/s"));

    WorkerController::parseLogLine(QStringLiteral("[2026-11-19 11:27:37] 3 A3 S0 R0 BLOCK SOLVED 3, 90.641 sec (2ms)"), rt);
    QCOMPARE(rt.accepted, 3);
    QCOMPARE(rt.stale, 0);
    QCOMPARE(rt.rejected, 0);

    WorkerController::parseLogLine(QStringLiteral("[2026-11-19 11:28:03] CPU temp: curr 33 C max 33"), rt);
    QCOMPARE(rt.cpuTempC, 33);

    WorkerController::parseLogLine(QStringLiteral("ADAM Auto-Register deferred: wallet already holds an active seat"), rt);
    QVERIFY(rt.warningDeferred);
    QVERIFY(rt.warningDeferredMsg.contains(QStringLiteral("active seat")));

    WorkerController::parseLogLine(QStringLiteral("ADAM miner: Waiting for next block or election..."), rt);
    QCOMPARE(rt.status, WorkerStatus::Waiting);

    WorkerRuntime gpuRt;
    WorkerController::parseLogLine(QStringLiteral("GPU #0: RTX 3090 Ti, 125.40 MH/s"), gpuRt);
    QCOMPARE(gpuRt.hashrateKhs, 125400.0);
    WorkerController::parseLogLine(QStringLiteral("accepted: 4/5 (80.00%), 125.40 MH/s yes!"), gpuRt);
    QCOMPARE(gpuRt.accepted, 4);
    QCOMPARE(gpuRt.rejected, 1);
}

void TestCore::testParseCmdlineAndSplitArgs()
{
    DiscoveredProcess cpuDp;
    const QStringList cpuArgs{
        QStringLiteral("/home/bedri/Coin-Projects/cpuminer-opt/cpuminer"),
        QStringLiteral("-a"), QStringLiteral("adam"),
        QStringLiteral("--url=http://127.0.0.1:27998/"),
        QStringLiteral("--userpass=u:p"),
        QStringLiteral("--coinbase-addr=KTdHzRF4e71xcU1ea8tePLmZNijUnH6ZNYm"),
        QStringLiteral("-t"), QStringLiteral("2")
    };
    QVERIFY(ProcessScanner::parseCmdline(cpuArgs, cpuDp));
    QCOMPARE(cpuDp.engine, MinerEngine::CpuMiner);
    QCOMPARE(cpuDp.coinbaseAddress, QStringLiteral("KTdHzRF4e71xcU1ea8tePLmZNijUnH6ZNYm"));
    QCOMPARE(cpuDp.threads, 2);

    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    const QString cfgPath = tmpDir.filePath(QStringLiteral("gpu.json"));
    QVERIFY(ConfigStore::writeFile0600(
        cfgPath,
        QByteArray("{\"algo\":\"adam\",\"url\":\"http://127.0.0.1:27998/\",\"coinbase-addr\":\"KTtestGpu123\",\"devices\":\"0,1\",\"intensity\":21.5}")));

    DiscoveredProcess gpuDp;
    QVERIFY(ProcessScanner::parseCmdline({QStringLiteral("/home/bedri/Coin-Projects/ccminer/ccminer"), QStringLiteral("-c"), cfgPath}, gpuDp));
    QCOMPARE(gpuDp.engine, MinerEngine::CcMiner);
    QCOMPARE(gpuDp.coinbaseAddress, QStringLiteral("KTtestGpu123"));
    QCOMPARE(gpuDp.gpuDevices, QStringLiteral("0,1"));
    QCOMPARE(gpuDp.gpuIntensity, 21.5);

    const QStringList split = WorkerController::splitExtraArgs(QStringLiteral("-q --coinbase-sig=\"KristaTech Node\""));
    QCOMPARE(split.size(), 2);
    QCOMPARE(split[0], QStringLiteral("-q"));
    QCOMPARE(split[1], QStringLiteral("--coinbase-sig=KristaTech Node"));
}

QTEST_MAIN(TestCore)
#include "test_core.moc"
