#include "config_store.h"
#include "models.h"
#include "process_scanner.h"
#include "worker_controller.h"

#include <QDir>
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
    void testAutoStopOnDeferred();
    void testAutoStartStoppedPodmanContainer();
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

    WorkerController::parseLogLine(QStringLiteral("ADAM Auto-Register: Miner KTLht4hN is active in ADAM pool. Next check in 30m."), rt);
    QVERIFY(!rt.warningDeferred);
    QVERIFY(rt.warningDeferredMsg.isEmpty());

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

void TestCore::testAutoStopOnDeferred()
{
#ifndef Q_OS_WIN
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    const QString mockMiner = tmpDir.filePath(QStringLiteral("mock_deferred_miner.sh"));
    {
        QFile f(mockMiner);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("#!/bin/sh\n"
                "echo \"[2026-11-20 00:22:04] ADAM Auto-Register deferred: This wallet already holds an active 10-KRISTA PoW-Lock seat\"\n"
                "exec sleep 30\n");
        f.close();
        QVERIFY(f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                                 | QFileDevice::ReadGroup | QFileDevice::ExeGroup
                                 | QFileDevice::ReadOther | QFileDevice::ExeOther));
    }

    ConfigStore store(tmpDir.filePath(QStringLiteral("cfg")));
    RpcClient rpc;
    WorkerController controller(&store, &rpc);
    controller.loadInitialState(/*autoDiscoverIfEmpty=*/false);
    QCOMPARE(controller.autoStopOnDeferred(), false);

    WorkerProfile p;
    p.id = QStringLiteral("mock_def_1");
    p.name = QStringLiteral("Mock Deferred");
    p.engine = MinerEngine::CpuMiner;
    p.binaryPath = mockMiner;
    p.targetType = TargetType::CustomRpc;
    p.rpcUrl = QStringLiteral("http://127.0.0.1:27998/");
    p.rpcUser = QStringLiteral("u");
    p.rpcPassword = QStringLiteral("p");
    p.coinbaseAddress = QStringLiteral("KTLht4hN");
    controller.addProfile(p);

    QSignalSpy autoStoppedSpy(&controller, &WorkerController::workerAutoStopped);

    // 1. Default OFF: worker emits deferred warning in log but continues running
    controller.startWorker(p.id);
    QTRY_VERIFY_WITH_TIMEOUT(controller.runtimeById(p.id).warningDeferred, 4000);
    const WorkerRuntime rtWhileRunning = controller.runtimeById(p.id);
    QVERIFY(rtWhileRunning.pid > 0);
    QVERIFY(rtWhileRunning.status != WorkerStatus::Stopped);
    QCOMPARE(autoStoppedSpy.count(), 0);

    // 2. Enabling Auto-Stop Deferred immediately stops currently deferred running worker
    controller.setAutoStopOnDeferred(true);
    QCOMPARE(controller.autoStopOnDeferred(), true);
    const WorkerRuntime rtAfterToggle = controller.runtimeById(p.id);
    QCOMPARE(rtAfterToggle.status, WorkerStatus::Stopped);
    QCOMPARE(rtAfterToggle.pid, qint64(0));
    QCOMPARE(rtAfterToggle.userStopped, true);
    QCOMPARE(rtAfterToggle.warningDeferred, true);
    QCOMPARE(autoStoppedSpy.count(), 1);
    QVERIFY(rtAfterToggle.logLines.join(QLatin1Char('\n')).contains(QStringLiteral("[KMM] Auto-stopped (Auto-Register Deferred)")));

    // 3. Starting while enabled auto-stops automatically once log line is polled
    QTest::qWait(100);
    controller.startWorker(p.id);
    QTRY_COMPARE_WITH_TIMEOUT(autoStoppedSpy.count(), 2, 4000);
    const WorkerRuntime rtAfterRestart = controller.runtimeById(p.id);
    QCOMPARE(rtAfterRestart.status, WorkerStatus::Stopped);
    QCOMPARE(rtAfterRestart.userStopped, true);

    controller.stopWorker(p.id);
#else
    QSKIP("POSIX shell mock process test skipped on Windows");
#endif
}

void TestCore::testAutoStartStoppedPodmanContainer()
{
#ifndef Q_OS_WIN
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    const QString binDir = tmpDir.filePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkpath(binDir));

    const QString stateFile = tmpDir.filePath(QStringLiteral("pod_running"));
    const QString startLog = tmpDir.filePath(QStringLiteral("started.log"));
    const QString fakePodman = binDir + QStringLiteral("/podman");
    {
        QFile f(fakePodman);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray script = QStringLiteral(
            "#!/bin/sh\n"
            "STATE_FILE=\"%1\"\n"
            "LOG_FILE=\"%2\"\n"
            "cmd=\"$1\"\n"
            "case \"$cmd\" in\n"
            "  inspect)\n"
            "    if [ -f \"$STATE_FILE\" ]; then\n"
            "      echo \"true 42424\"\n"
            "    else\n"
            "      echo \"false 0\"\n"
            "    fi\n"
            "    exit 0\n"
            "    ;;\n"
            "  start)\n"
            "    echo \"$2\" >> \"$LOG_FILE\"\n"
            "    touch \"$STATE_FILE\"\n"
            "    exit 0\n"
            "    ;;\n"
            "  exec)\n"
            "    if [ ! -f \"$STATE_FILE\" ]; then\n"
            "      echo \"Error: can only create exec sessions on running containers: container state improper\" >&2\n"
            "      exit 125\n"
            "    fi\n"
            "    cat <<'EOF'\n"
            "rpcuser=poduser\n"
            "rpcpassword=podpass\n"
            "rpcport=27979\n"
            "EOF\n"
            "    exit 0\n"
            "    ;;\n"
            "  unshare)\n"
            "    shift 5\n"
            "    exec \"$@\"\n"
            "    ;;\n"
            "  *)\n"
            "    echo \"unexpected podman cmd: $*\" >&2\n"
            "    exit 2\n"
            "    ;;\n"
            "esac\n").arg(stateFile, startLog).toUtf8();
        f.write(script);
        f.close();
        QVERIFY(f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                                 | QFileDevice::ReadGroup | QFileDevice::ExeGroup
                                 | QFileDevice::ReadOther | QFileDevice::ExeOther));
    }

    const QString mockMiner = tmpDir.filePath(QStringLiteral("mock_podman_miner.sh"));
    {
        QFile f(mockMiner);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("#!/bin/sh\n"
                "echo \"[2026-11-20 01:00:00] ADAM miner: Waiting for next block or election...\"\n"
                "exec sleep 30\n");
        f.close();
        QVERIFY(f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                                 | QFileDevice::ReadGroup | QFileDevice::ExeGroup
                                 | QFileDevice::ReadOther | QFileDevice::ExeOther));
    }

    const QByteArray origPath = qgetenv("PATH");
    struct PathRestorer {
        QByteArray orig;
        ~PathRestorer() { qputenv("PATH", orig); }
    } restorePath{origPath};
    qputenv("PATH", binDir.toUtf8() + ':' + origPath);

    ConfigStore store(tmpDir.filePath(QStringLiteral("cfg")));
    RpcClient rpc;
    WorkerController controller(&store, &rpc);
    controller.loadInitialState(/*autoDiscoverIfEmpty=*/false);

    WorkerProfile p;
    p.id = QStringLiteral("pod_test_1");
    p.name = QStringLiteral("Podman Test");
    p.engine = MinerEngine::CpuMiner;
    p.binaryPath = mockMiner;
    p.targetType = TargetType::PodmanContainer;
    p.podmanName = QStringLiteral("krista-cold-test01");
    p.coinbaseAddress = QStringLiteral("KTXWGMQ6");
    p.autoRestart = false;
    controller.addProfile(p);

    struct StopGuard {
        WorkerController& c;
        QString id;
        ~StopGuard() { c.stopWorker(id); }
    } stopGuard{controller, p.id};

    controller.startWorker(p.id);

    const WorkerRuntime rtImmediately = controller.runtimeById(p.id);
    QCOMPARE(rtImmediately.status, WorkerStatus::Starting);
    QVERIFY(rtImmediately.logLines.join(QLatin1Char('\n'))
                .contains(QStringLiteral("Podman container 'krista-cold-test01' is stopped; starting container...")));

    QTRY_VERIFY_WITH_TIMEOUT(
        QFileInfo::exists(stateFile)
        && controller.runtimeById(p.id).pid > 0
        && (controller.runtimeById(p.id).status == WorkerStatus::Waiting
            || controller.runtimeById(p.id).status == WorkerStatus::Solving),
        4000);

    const WorkerRuntime rtRunning = controller.runtimeById(p.id);
    const QString fullLog = rtRunning.logLines.join(QLatin1Char('\n'));
    QVERIFY(fullLog.contains(QStringLiteral("Podman container 'krista-cold-test01' started.")));
    QVERIFY(fullLog.contains(QStringLiteral("[KMM] Started")));

    QFile logF(startLog);
    QVERIFY(logF.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(logF.readAll()).trimmed(), QStringLiteral("krista-cold-test01"));

    controller.stopWorker(p.id);
    QCOMPARE(controller.runtimeById(p.id).status, WorkerStatus::Stopped);
#else
    QSKIP("POSIX shell podman mock test skipped on Windows");
#endif
}

QTEST_MAIN(TestCore)
#include "test_core.moc"
