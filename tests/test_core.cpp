#include "config_store.h"
#include "models.h"
#include "process_scanner.h"
#include "rpc_client.h"
#include "worker_controller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

#ifndef Q_OS_WIN
#include <csignal>
#endif

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
    void testAutoReattachStalePodmanNetNs();
    void testCustomRpcLifecycleAndJsonRpc();
    void testRemoteSshLifecycleAndHealthCheck();
    void testKristaFairnessRpcAndSeatExpiry();
    void testSecurityHardening();
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

void TestCore::testAutoReattachStalePodmanNetNs()
{
#ifndef Q_OS_WIN
    const QString selfNs = ProcessScanner::readProcessNetNs(QCoreApplication::applicationPid());
    QVERIFY(selfNs.startsWith(QStringLiteral("net:[")));
    QVERIFY(ProcessScanner::readProcessNetNs(0).isEmpty());

    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    const QString binDir = tmpDir.filePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkpath(binDir));

    const QString pidFile = tmpDir.filePath(QStringLiteral("pod_pid"));
    const QString nsenterLog = tmpDir.filePath(QStringLiteral("nsenter_pids.log"));
    {
        QFile pf(pidFile);
        QVERIFY(pf.open(QIODevice::WriteOnly | QIODevice::Truncate));
        pf.write("42424\n");
    }

    const QString fakePodman = binDir + QStringLiteral("/podman");
    {
        QFile f(fakePodman);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray script = QStringLiteral(
            "#!/bin/sh\n"
            "PID_FILE=\"%1\"\n"
            "NSENTER_LOG=\"%2\"\n"
            "cmd=\"$1\"\n"
            "case \"$cmd\" in\n"
            "  inspect)\n"
            "    shift\n"
            "    CUR_PID=\"$(cat \"$PID_FILE\")\"\n"
            "    FMT=\"\"\n"
            "    NAMES=\"\"\n"
            "    while [ \"$#\" -gt 0 ]; do\n"
            "      case \"$1\" in\n"
            "        --format)\n"
            "          FMT=\"$2\"\n"
            "          shift 2\n"
            "          ;;\n"
            "        *)\n"
            "          NAMES=\"$NAMES $1\"\n"
            "          shift\n"
            "          ;;\n"
            "      esac\n"
            "    done\n"
            "    case \"$FMT\" in\n"
            "      *Name*)\n"
            "        for n in $NAMES; do\n"
            "          echo \"$n true $CUR_PID\"\n"
            "        done\n"
            "        ;;\n"
            "      *)\n"
            "        echo \"true $CUR_PID\"\n"
            "        ;;\n"
            "    esac\n"
            "    exit 0\n"
            "    ;;\n"
            "  exec)\n"
            "    cat <<'EOF'\n"
            "rpcuser=poduser\n"
            "rpcpassword=podpass\n"
            "rpcport=27979\n"
            "EOF\n"
            "    exit 0\n"
            "    ;;\n"
            "  unshare)\n"
            "    echo \"$4\" >> \"$NSENTER_LOG\"\n"
            "    shift 5\n"
            "    exec \"$@\"\n"
            "    ;;\n"
            "  *)\n"
            "    echo \"unexpected podman cmd: $*\" >&2\n"
            "    exit 2\n"
            "    ;;\n"
            "esac\n").arg(pidFile, nsenterLog).toUtf8();
        f.write(script);
        f.close();
        QVERIFY(f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                                 | QFileDevice::ReadGroup | QFileDevice::ExeGroup
                                 | QFileDevice::ReadOther | QFileDevice::ExeOther));
    }

    const QString mockMiner = tmpDir.filePath(QStringLiteral("cpuminer-mock.sh"));
    {
        QFile f(mockMiner);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("#!/bin/sh\n"
                "echo \"[2026-11-20 17:00:00] ADAM miner: Waiting for next block or election...\"\n"
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
    p.id = QStringLiteral("pod_ns_1");
    p.name = QStringLiteral("Podman NetNs Test");
    p.engine = MinerEngine::CpuMiner;
    p.binaryPath = mockMiner;
    p.targetType = TargetType::PodmanContainer;
    p.podmanName = QStringLiteral("krista-cold-test02");
    p.coinbaseAddress = QStringLiteral("KTXWGMQ6");
    p.autoRestart = true;
    controller.addProfile(p);

    struct StopGuard {
        WorkerController& c;
        QString id;
        ~StopGuard() { c.stopWorker(id); }
    } stopGuard{controller, p.id};

    qint64 firstMinerPid = 0;
    controller.setNetNsResolverForTest([&firstMinerPid](qint64 pid) -> QString {
        if (pid == 42424) return QStringLiteral("net:[10001]");
        if (pid == 53535) return QStringLiteral("net:[20002]");
        if (firstMinerPid > 0 && pid == firstMinerPid) return QStringLiteral("net:[10001]");
        if (pid > 0) return QStringLiteral("net:[20002]");
        return {};
    });

    controller.startWorker(p.id);
    QTRY_VERIFY_WITH_TIMEOUT(
        controller.runtimeById(p.id).pid > 0
        && (controller.runtimeById(p.id).status == WorkerStatus::Waiting
            || controller.runtimeById(p.id).status == WorkerStatus::Solving),
        4000);

    firstMinerPid = controller.runtimeById(p.id).pid;
    QVERIFY(firstMinerPid > 0);

    // Same netns as container 42424: should NOT trigger restart
    controller.checkPodmanNamespacesNow();
    QTest::qWait(250);
    QCOMPARE(controller.runtimeById(p.id).pid, firstMinerPid);

    // Simulate container restart into new PID/netns
    {
        QFile pf(pidFile);
        QVERIFY(pf.open(QIODevice::WriteOnly | QIODevice::Truncate));
        pf.write("53535\n");
    }

    controller.checkPodmanNamespacesNow();

    QTRY_VERIFY_WITH_TIMEOUT(
        controller.runtimeById(p.id).pid > 0
        && controller.runtimeById(p.id).pid != firstMinerPid
        && (controller.runtimeById(p.id).status == WorkerStatus::Waiting
            || controller.runtimeById(p.id).status == WorkerStatus::Solving),
        4000);

    const WorkerRuntime rtAfter = controller.runtimeById(p.id);
    const QString fullLog = rtAfter.logLines.join(QLatin1Char('\n'));
    QVERIFY(fullLog.contains(
        QStringLiteral("Podman container 'krista-cold-test02' restarted (stale netns net:[10001] != net:[20002]); reattaching worker...")));

    QFile nf(nsenterLog);
    QVERIFY(nf.open(QIODevice::ReadOnly));
    const QStringList seenTargets = QString::fromUtf8(nf.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(seenTargets, (QStringList{QStringLiteral("42424"), QStringLiteral("53535")}));

    QTRY_VERIFY_WITH_TIMEOUT(!ProcessScanner::isPidAlive(firstMinerPid), 4000);

    controller.stopWorker(p.id);
    QCOMPARE(controller.runtimeById(p.id).status, WorkerStatus::Stopped);
#else
    QSKIP("POSIX podman netns test skipped on Windows");
#endif
}

void TestCore::testCustomRpcLifecycleAndJsonRpc()
{
    QCOMPARE(normalizeRpcUrl(QStringLiteral(" 127.0.0.1:27998 ")), QStringLiteral("http://127.0.0.1:27998/"));
    QCOMPARE(normalizeRpcUrl(QStringLiteral("HTTPS://Node.Example:8443/rpc")), QStringLiteral("https://node.example:8443/rpc"));
    QVERIFY(normalizeRpcUrl(QStringLiteral("ftp://bad")).isEmpty());
    QVERIFY(normalizeRpcUrl(QStringLiteral("http://")).isEmpty());
    QVERIFY(isLoopbackRpcUrl(QStringLiteral("127.0.0.1:27998")));
    QVERIFY(!isLoopbackRpcUrl(QStringLiteral("http://198.51.100.10:27998/")));

    WorkerProfile badUrlProf;
    badUrlProf.targetType = TargetType::CustomRpc;
    badUrlProf.rpcUrl = QStringLiteral("ftp://bad");
    badUrlProf.rpcUser = QStringLiteral("rpcu");
    badUrlProf.rpcPassword = QStringLiteral("rpcp");
    QVERIFY(!ConfigStore::resolveCredentials(badUrlProf).valid);

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    const quint16 port = server.serverPort();

    bool internalGen = true;
    int setGenerateCalls = 0;
    const QByteArray expectedAuth = "Basic " + QByteArray("rpcu:rpcp").toBase64();
    QHash<QTcpSocket*, QByteArray> buffers;

    QObject::connect(&server, &QTcpServer::newConnection, &server, [&]() {
        while (QTcpSocket* sock = server.nextPendingConnection()) {
            QObject::connect(sock, &QTcpSocket::disconnected, sock, [sock, &buffers]() {
                buffers.remove(sock);
                sock->deleteLater();
            });
            QObject::connect(sock, &QTcpSocket::readyRead, sock, [sock, &buffers, &internalGen, &setGenerateCalls, expectedAuth]() {
                QByteArray& buf = buffers[sock];
                buf.append(sock->readAll());
                const int hdrEnd = buf.indexOf("\r\n\r\n");
                if (hdrEnd < 0) return;
                const QByteArray hdrs = buf.left(hdrEnd);
                int contentLen = 0;
                QByteArray authVal;
                for (const QByteArray& line : hdrs.split('\n')) {
                    const QByteArray trimmedLine = line.trimmed();
                    const QByteArray low = trimmedLine.toLower();
                    if (low.startsWith("content-length:")) {
                        contentLen = trimmedLine.mid(15).trimmed().toInt();
                    } else if (low.startsWith("authorization:")) {
                        authVal = trimmedLine.mid(14).trimmed();
                    }
                }
                if (buf.size() < hdrEnd + 4 + contentLen) return;
                const QByteArray body = buf.mid(hdrEnd + 4, contentLen);
                buf.clear();

                if (authVal != expectedAuth) {
                    const QByteArray resp =
                        "HTTP/1.1 401 Unauthorized\r\n"
                        "Content-Length: 0\r\n"
                        "Connection: close\r\n\r\n";
                    sock->write(resp);
                    sock->disconnectFromHost();
                    return;
                }

                const QJsonObject reqObj = QJsonDocument::fromJson(body).object();
                const QString method = reqObj.value(QStringLiteral("method")).toString();
                const QJsonArray params = reqObj.value(QStringLiteral("params")).toArray();

                QJsonObject respObj;
                respObj.insert(QStringLiteral("id"), reqObj.value(QStringLiteral("id")));
                respObj.insert(QStringLiteral("error"), QJsonValue::Null);

                if (method == QStringLiteral("getmininginfo")) {
                    QJsonObject res;
                    res.insert(QStringLiteral("blocks"), 8123);
                    res.insert(QStringLiteral("networkhashps"), 123456.0);
                    res.insert(QStringLiteral("generate"), internalGen);
                    res.insert(QStringLiteral("genproclimit"), internalGen ? 4 : 0);
                    respObj.insert(QStringLiteral("result"), res);
                } else if (method == QStringLiteral("getadamminers")) {
                    QJsonObject seat;
                    seat.insert(QStringLiteral("address"), QStringLiteral("KTCustomAddr000000000000000000000001"));
                    seat.insert(QStringLiteral("track"), QStringLiteral("coin_lock"));
                    seat.insert(QStringLiteral("unlock_height"), 10000);
                    respObj.insert(QStringLiteral("result"), QJsonArray{seat});
                } else if (method == QStringLiteral("setgenerate")) {
                    internalGen = params.first().toBool(false);
                    ++setGenerateCalls;
                    respObj.insert(QStringLiteral("result"), QJsonValue::Null);
                } else {
                    QJsonObject errObj;
                    errObj.insert(QStringLiteral("code"), -32601);
                    errObj.insert(QStringLiteral("message"), QStringLiteral("Method not found"));
                    respObj.insert(QStringLiteral("result"), QJsonValue::Null);
                    respObj.insert(QStringLiteral("error"), errObj);
                }

                const QByteArray payload = QJsonDocument(respObj).toJson(QJsonDocument::Compact);
                const QByteArray resp =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json\r\n"
                    "Content-Length: " + QByteArray::number(payload.size()) + "\r\n"
                    "Connection: close\r\n\r\n" + payload;
                sock->write(resp);
                sock->disconnectFromHost();
            });
        }
    });

    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    ConfigStore store(tmpDir.filePath(QStringLiteral("cfg")));
    RpcClient rpc;

    // 1. Wrong password -> HTTP 401 -> unreachable
    WorkerProfile wrongProf;
    wrongProf.id = QStringLiteral("custom_bad_auth");
    wrongProf.name = QStringLiteral("Bad Auth RPC");
    wrongProf.targetType = TargetType::CustomRpc;
    wrongProf.rpcUrl = QStringLiteral("127.0.0.1:%1").arg(port);
    wrongProf.rpcUser = QStringLiteral("rpcu");
    wrongProf.rpcPassword = QStringLiteral("wrongpass");
    rpc.refreshTargets({wrongProf});
    QTRY_VERIFY_WITH_TIMEOUT(rpc.statusForTarget(wrongProf.targetKey()).checked, 4000);
    QCOMPARE(rpc.statusForTarget(wrongProf.targetKey()).reachable, false);

    // 2. Valid credentials -> reachable + pool seats populated
    WorkerProfile p;
    p.id = QStringLiteral("custom_1");
    p.name = QStringLiteral("Custom Node 1");
    p.engine = MinerEngine::CpuMiner;
    p.targetType = TargetType::CustomRpc;
    p.rpcUrl = QStringLiteral("127.0.0.1:%1").arg(port);
    p.rpcUser = QStringLiteral("rpcu");
    p.rpcPassword = QStringLiteral("rpcp");
    p.coinbaseAddress = QStringLiteral("KTCustomAddr000000000000000000000001");
    p.autoRestart = false;
    p.disableInternalGenOnStart = true;

    rpc.refreshTargets({p});
    QTRY_VERIFY_WITH_TIMEOUT(
        rpc.statusForTarget(p.targetKey()).reachable && !rpc.allKnownSeats().isEmpty(),
        4000);
    const NodeMiningStatus st = rpc.statusForTarget(p.targetKey());
    QCOMPARE(st.blockHeight, qint64(8123));
    QCOMPARE(st.internalGenerate, true);
    const PoolSeatInfo seat = rpc.seatInfoForAddress(p.coinbaseAddress);
    QCOMPARE(seat.registered, true);
    QCOMPARE(seat.track, QStringLiteral("coin_lock"));

    // 3. Explicit setInternalGenerate(false)
    bool sgDone = false;
    bool sgOk = false;
    rpc.setInternalGenerate(p, false, [&](bool ok, const QString&) {
        sgDone = true;
        sgOk = ok;
    });
    QTRY_VERIFY_WITH_TIMEOUT(sgDone, 4000);
    QVERIFY(sgOk);
    QCOMPARE(internalGen, false);
    QCOMPARE(setGenerateCalls, 1);

#ifndef Q_OS_WIN
    // Reset server generate=true and verify disableInternalGenOnStart triggers setgenerate(false) on launch
    internalGen = true;
    const QString mockMiner = tmpDir.filePath(QStringLiteral("mock_custom_miner.sh"));
    {
        QFile f(mockMiner);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("#!/bin/sh\n"
                "echo \"[2026-11-20 17:15:00] New Block 8124\"\n"
                "echo \"[2026-11-20 17:15:01] Miner TTF @ 42.50 kh/s\"\n"
                "echo \"[2026-11-20 17:15:02] 1 A1 S0 R0 BLOCK SOLVED\"\n"
                "exec sleep 30\n");
        f.close();
        QVERIFY(f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                                 | QFileDevice::ReadGroup | QFileDevice::ExeGroup
                                 | QFileDevice::ReadOther | QFileDevice::ExeOther));
    }
    p.binaryPath = mockMiner;

    WorkerController controller(&store, &rpc);
    controller.loadInitialState(/*autoDiscoverIfEmpty=*/false);
    controller.addProfile(p);

    struct StopGuard {
        WorkerController& c;
        QString id;
        ~StopGuard() { c.stopWorker(id); }
    } stopGuard{controller, p.id};

    controller.startWorker(p.id);
    QTRY_VERIFY_WITH_TIMEOUT(
        controller.runtimeById(p.id).status == WorkerStatus::Solving
        && controller.runtimeById(p.id).accepted == 1
        && !internalGen,
        4000);

    QFile rtJson(store.runtimeJsonPath(p.id));
    QVERIFY(rtJson.open(QIODevice::ReadOnly));
    const QJsonObject cfgObj = QJsonDocument::fromJson(rtJson.readAll()).object();
    QCOMPARE(cfgObj.value(QStringLiteral("url")).toString(), QStringLiteral("http://127.0.0.1:%1/").arg(port));
    QCOMPARE(cfgObj.value(QStringLiteral("userpass")).toString(), QStringLiteral("rpcu:rpcp"));

    const WorkerRuntime rt = controller.runtimeById(p.id);
    QVERIFY(rt.hashrateKhs > 40.0);
    QCOMPARE(rt.lastBlock, qint64(8124));
    controller.stopWorker(p.id);
    QCOMPARE(controller.runtimeById(p.id).status, WorkerStatus::Stopped);

    // 4. Process adoption: CustomRpc adopts non-loopback miner; LocalWallet must NOT hijack it
    const QString adoptBin = tmpDir.filePath(QStringLiteral("cpuminer-adopt"));
    QProcess adoptProc;
    adoptProc.start(QStringLiteral("bash"),
                    {QStringLiteral("-c"),
                     QStringLiteral("exec -a \"$1\" /bin/sh -s -- -a adam --url=http://198.51.100.10:27998/ --coinbase-addr=KTAdoptTest123"),
                     QStringLiteral("bash"),
                     adoptBin});
    QVERIFY(adoptProc.waitForStarted(3000));
    struct ProcGuard {
        QProcess& proc;
        ~ProcGuard() {
            if (proc.state() != QProcess::NotRunning) {
                proc.closeWriteChannel();
                proc.kill();
                proc.waitForFinished(2000);
            }
        }
    } adoptGuard{adoptProc};

    bool foundAsCustom = false;
    const qint64 adoptPid = adoptProc.processId();
    QTRY_VERIFY_WITH_TIMEOUT([&]() {
        for (const DiscoveredProcess& dp : ProcessScanner::scanRunningMiners()) {
            if (dp.pid == adoptPid) {
                if (dp.targetType == TargetType::CustomRpc
                    && dp.rpcUrl == QStringLiteral("http://198.51.100.10:27998/")) {
                    foundAsCustom = true;
                }
                return foundAsCustom;
            }
        }
        return false;
    }(), 3000);
    QVERIFY(foundAsCustom);

    ConfigStore localStore(tmpDir.filePath(QStringLiteral("cfg_local")));
    RpcClient localRpc;
    WorkerController localCtrl(&localStore, &localRpc);
    localCtrl.loadInitialState(false);
    WorkerProfile localProf;
    localProf.id = QStringLiteral("local_only");
    localProf.name = QStringLiteral("Local Only");
    localProf.engine = MinerEngine::CpuMiner;
    localProf.targetType = TargetType::LocalWallet;
    localProf.coinbaseAddress = QStringLiteral("KTAdoptTest123");
    localCtrl.addProfile(localProf);
    QCOMPARE(localCtrl.adoptRunningProcesses(false), 0);

    ConfigStore customStore(tmpDir.filePath(QStringLiteral("cfg_custom")));
    RpcClient customRpc;
    WorkerController customCtrl(&customStore, &customRpc);
    customCtrl.loadInitialState(false);
    WorkerProfile customProf;
    customProf.id = QStringLiteral("custom_adopt");
    customProf.name = QStringLiteral("Custom Adopt");
    customProf.engine = MinerEngine::CpuMiner;
    customProf.targetType = TargetType::CustomRpc;
    customProf.rpcUrl = QStringLiteral("198.51.100.10:27998");
    customProf.rpcUser = QStringLiteral("u");
    customProf.rpcPassword = QStringLiteral("p");
    customProf.coinbaseAddress = QStringLiteral("KTAdoptTest123");
    customCtrl.addProfile(customProf);
    QCOMPARE(customCtrl.adoptRunningProcesses(false), 1);
    QCOMPARE(customCtrl.runtimeById(customProf.id).pid, adoptPid);
#endif
}

void TestCore::testRemoteSshLifecycleAndHealthCheck()
{
#ifndef Q_OS_WIN
    QVERIFY(!isValidSshTarget(QString()));
    QVERIFY(!isValidSshTarget(QStringLiteral("-oProxyCommand=evil")));
    QVERIFY(!isValidSshTarget(QStringLiteral("user@host;rm -rf /")));
    QVERIFY(isValidSshTarget(QStringLiteral("miner@192.168.1.50")));

    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    const QString binDir = tmpDir.filePath(QStringLiteral("bin"));
    const QString emptyBinDir = tmpDir.filePath(QStringLiteral("empty_bin"));
    const QString remoteHome = tmpDir.filePath(QStringLiteral("remote_home"));
    const QString remoteBinDir = remoteHome + QStringLiteral("/bin");
    const QString sshAuditLog = tmpDir.filePath(QStringLiteral("ssh_audit.log"));
    const QString setGenLog = tmpDir.filePath(QStringLiteral("ssh_setgen.log"));
    QVERIFY(QDir().mkpath(binDir));
    QVERIFY(QDir().mkpath(emptyBinDir));
    QVERIFY(QDir().mkpath(remoteBinDir));

    const QString fakeSsh = binDir + QStringLiteral("/ssh");
    {
        QFile f(fakeSsh);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray script = QStringLiteral(
            "#!/bin/sh\n"
            "REMOTE_HOME=\"%1\"\n"
            "AUDIT_LOG=\"%2\"\n"
            "TARGET=\"\"\n"
            "CMD=\"\"\n"
            "SAW_DASHDASH=0\n"
            "SEEN_BATCHMODE=0\n"
            "SEEN_NOTTY=0\n"
            "SEEN_CLEARFWD=0\n"
            "PORT_VAL=\"\"\n"
            "while [ \"$#\" -gt 0 ]; do\n"
            "  if [ \"$SAW_DASHDASH\" -eq 1 ]; then\n"
            "    TARGET=\"$1\"\n"
            "    shift\n"
            "    CMD=\"$*\"\n"
            "    break\n"
            "  fi\n"
            "  case \"$1\" in\n"
            "    -o)\n"
            "      if [ \"$2\" = \"BatchMode=yes\" ]; then SEEN_BATCHMODE=1; fi\n"
            "      if [ \"$2\" = \"RequestTTY=no\" ]; then SEEN_NOTTY=1; fi\n"
            "      if [ \"$2\" = \"ClearAllForwardings=yes\" ]; then SEEN_CLEARFWD=1; fi\n"
            "      shift 2\n"
            "      ;;\n"
            "    -p)\n"
            "      PORT_VAL=\"$2\"\n"
            "      shift 2\n"
            "      ;;\n"
            "    --)\n"
            "      SAW_DASHDASH=1\n"
            "      shift\n"
            "      ;;\n"
            "    -*)\n"
            "      echo \"unexpected ssh flag without --: $1\" >&2\n"
            "      exit 99\n"
            "      ;;\n"
            "    *)\n"
            "      TARGET=\"$1\"\n"
            "      shift\n"
            "      CMD=\"$*\"\n"
            "      break\n"
            "      ;;\n"
            "  esac\n"
            "done\n"
            "if [ \"$SAW_DASHDASH\" -ne 1 ] || [ \"$SEEN_BATCHMODE\" -ne 1 ] || [ \"$SEEN_NOTTY\" -ne 1 ] || [ \"$SEEN_CLEARFWD\" -ne 1 ] || [ -z \"$TARGET\" ]; then\n"
            "  echo \"ssh wrapper assertion failed\" >&2\n"
            "  exit 98\n"
            "fi\n"
            "echo \"${PORT_VAL}|${TARGET}|${CMD}\" >> \"$AUDIT_LOG\"\n"
            "mkdir -p \"$REMOTE_HOME\"\n"
            "exec env HOME=\"$REMOTE_HOME\" PATH=\"$REMOTE_HOME/bin:$PATH\" /bin/sh -c \"$CMD\"\n")
            .arg(remoteHome, sshAuditLog).toUtf8();
        f.write(script);
        f.close();
        QVERIFY(f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                                 | QFileDevice::ReadGroup | QFileDevice::ExeGroup
                                 | QFileDevice::ReadOther | QFileDevice::ExeOther));
    }

    const QString fakeCli = remoteBinDir + QStringLiteral("/kristatech-cli");
    {
        QFile f(fakeCli);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray script = QStringLiteral(
            "#!/bin/sh\n"
            "SETGEN_LOG=\"%1\"\n"
            "case \"$1\" in\n"
            "  getmininginfo)\n"
            "    echo '{\"blocks\":8125,\"networkhashps\":99999,\"generate\":true,\"genproclimit\":2}'\n"
            "    exit 0\n"
            "    ;;\n"
            "  getadamminers)\n"
            "    echo '[{\"address\":\"KTSshAddr0000000000000000000000001\",\"track\":\"pow_lock\",\"unlock_height\":10200}]'\n"
            "    exit 0\n"
            "    ;;\n"
            "  setgenerate)\n"
            "    echo \"$2\" >> \"$SETGEN_LOG\"\n"
            "    exit 0\n"
            "    ;;\n"
            "  *)\n"
            "    echo \"unknown cli method: $1\" >&2\n"
            "    exit 1\n"
            "    ;;\n"
            "esac\n").arg(setGenLog).toUtf8();
        f.write(script);
        f.close();
        QVERIFY(f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                                 | QFileDevice::ReadGroup | QFileDevice::ExeGroup
                                 | QFileDevice::ReadOther | QFileDevice::ExeOther));
    }

    const QString fakeMiner = remoteBinDir + QStringLiteral("/cpuminer");
    {
        QFile f(fakeMiner);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("#!/bin/sh\n"
                "echo \"[2026-11-20 17:15:00] New Block 8125\"\n"
                "echo \"[2026-11-20 17:15:01] Miner TTF @ 64.25 kh/s\"\n"
                "echo \"[2026-11-20 17:15:02] 2 A2 S0 R0 BLOCK SOLVED\"\n"
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

    struct RemotePidCleanup {
        QString kmmDir;
        ~RemotePidCleanup() {
            const QDir d(kmmDir);
            for (const QString& fn : d.entryList({QStringLiteral("*.pid")}, QDir::Files)) {
                QFile pf(d.filePath(fn));
                if (pf.open(QIODevice::ReadOnly)) {
                    const qint64 pid = QString::fromUtf8(pf.readAll()).trimmed().toLongLong();
                    if (pid > 1) ::kill(static_cast<pid_t>(pid), SIGKILL);
                }
            }
        }
    } remoteCleanup{remoteHome + QStringLiteral("/.kristatech-mm")};

    ConfigStore store(tmpDir.filePath(QStringLiteral("cfg")));
    RpcClient rpc;
    WorkerController controller(&store, &rpc);
    controller.loadInitialState(false);

    // 1. Invalid SSH target rejected cleanly by RpcClient & WorkerController
    WorkerProfile badSsh;
    badSsh.id = QStringLiteral("ssh_bad");
    badSsh.name = QStringLiteral("Bad SSH");
    badSsh.targetType = TargetType::RemoteSsh;
    badSsh.sshTarget = QStringLiteral("-oProxyCommand=evil");
    badSsh.rpcUrl = QStringLiteral("127.0.0.1:27998");
    badSsh.rpcUser = QStringLiteral("u");
    badSsh.rpcPassword = QStringLiteral("p");
    controller.addProfile(badSsh);

    bool badRpcDone = false;
    bool badRpcOk = true;
    QString badRpcErr;
    rpc.setInternalGenerate(badSsh, false, [&](bool ok, const QString& err) {
        badRpcDone = true;
        badRpcOk = ok;
        badRpcErr = err;
    });
    QTRY_VERIFY_WITH_TIMEOUT(badRpcDone, 2000);
    QVERIFY(!badRpcOk);
    QVERIFY(badRpcErr.contains(QStringLiteral("Invalid SSH target")));

    controller.startWorker(badSsh.id);
    QCOMPARE(controller.runtimeById(badSsh.id).status, WorkerStatus::Error);
    controller.removeProfile(badSsh.id, false);

    // 2. Valid SSH profile -> RpcClient refreshTargets + pool seats + setInternalGenerate
    WorkerProfile p;
    p.id = QStringLiteral("ssh_1");
    p.name = QStringLiteral("Remote Rig 1");
    p.engine = MinerEngine::CpuMiner;
    p.targetType = TargetType::RemoteSsh;
    p.sshTarget = QStringLiteral("miner@192.168.1.50");
    p.sshPort = 2222;
    p.remoteBinaryPath = QStringLiteral("~/bin/cpuminer");
    p.rpcUrl = QStringLiteral("127.0.0.1:27998");
    p.rpcUser = QStringLiteral("sshrpcu");
    p.rpcPassword = QStringLiteral("sshrpcp");
    p.coinbaseAddress = QStringLiteral("KTSshAddr0000000000000000000000001");
    p.autoRestart = true;
    p.disableInternalGenOnStart = true;
    controller.addProfile(p);

    struct StopGuard {
        WorkerController& c;
        QString id;
        ~StopGuard() { c.stopWorker(id); }
    } stopGuard{controller, p.id};

    rpc.refreshTargets({p});
    QTRY_VERIFY_WITH_TIMEOUT(
        rpc.statusForTarget(p.targetKey()).reachable && !rpc.allKnownSeats().isEmpty(),
        4000);
    QCOMPARE(rpc.statusForTarget(p.targetKey()).blockHeight, qint64(8125));
    QCOMPARE(rpc.statusForTarget(p.targetKey()).internalGenerate, true);
    QCOMPARE(rpc.seatInfoForAddress(p.coinbaseAddress).registered, true);
    QCOMPARE(rpc.seatInfoForAddress(p.coinbaseAddress).track, QStringLiteral("pow_lock"));

    // 3. Start remote SSH worker (~ expansion + 0600 JSON + live tail-F + workersChanged + disableInternalGen)
    QSignalSpy changedSpy(&controller, &WorkerController::workersChanged);
    controller.startWorker(p.id);
    QTRY_VERIFY_WITH_TIMEOUT(
        controller.runtimeById(p.id).status == WorkerStatus::Solving
        && controller.runtimeById(p.id).accepted == 2
        && controller.runtimeById(p.id).pid > 0,
        4000);

    const WorkerRuntime rtLive = controller.runtimeById(p.id);
    QVERIFY(rtLive.hashrateKhs > 60.0);
    QCOMPARE(rtLive.lastBlock, qint64(8125));
    QVERIFY(changedSpy.count() >= 2);

    const QString remoteJsonPath = remoteHome + QStringLiteral("/.kristatech-mm/ssh_1.json");
    QVERIFY(QFileInfo::exists(remoteJsonPath));
    QVERIFY((QFileInfo(remoteJsonPath).permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther)) == 0);

    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(setGenLog), 4000);

    // 4. Crash detection & automatic backoff restart for RemoteSsh
    const qint64 firstRemotePid = rtLive.pid;
    QVERIFY(::kill(static_cast<pid_t>(firstRemotePid), SIGTERM) == 0);
    QTRY_VERIFY_WITH_TIMEOUT(!ProcessScanner::isPidAlive(firstRemotePid), 3000);

    controller.checkRemoteSshHealthNow();
    QTRY_COMPARE_WITH_TIMEOUT(controller.runtimeById(p.id).status, WorkerStatus::Restarting, 4000);
    QVERIFY(controller.runtimeById(p.id).restartAttempts >= 1);

    // Backoff #1 is 3s; onHealthCheckTick should auto-restart SSH worker with a new PID
    QTRY_VERIFY_WITH_TIMEOUT(
        controller.runtimeById(p.id).pid > 0
        && controller.runtimeById(p.id).pid != firstRemotePid
        && controller.runtimeById(p.id).status == WorkerStatus::Solving,
        6000);

    // 5. Clean stopWorker removes remote files & stops PID
    const qint64 secondRemotePid = controller.runtimeById(p.id).pid;
    controller.stopWorker(p.id);
    QCOMPARE(controller.runtimeById(p.id).status, WorkerStatus::Stopped);
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(remoteJsonPath) && !ProcessScanner::isPidAlive(secondRemotePid), 4000);

    // 6. AutoRestart=false crash -> transitions to Error instead of Restarting
    p.autoRestart = false;
    controller.updateProfile(p);
    controller.startWorker(p.id);
    QTRY_VERIFY_WITH_TIMEOUT(
        controller.runtimeById(p.id).pid > 0 && controller.runtimeById(p.id).status == WorkerStatus::Solving,
        4000);
    const qint64 thirdRemotePid = controller.runtimeById(p.id).pid;
    QVERIFY(::kill(static_cast<pid_t>(thirdRemotePid), SIGTERM) == 0);
    QTRY_VERIFY_WITH_TIMEOUT(!ProcessScanner::isPidAlive(thirdRemotePid), 3000);
    controller.checkRemoteSshHealthNow();
    QTRY_COMPARE_WITH_TIMEOUT(controller.runtimeById(p.id).status, WorkerStatus::Error, 4000);

    // 7. Immediate remote startup failure (nonexistent remote binary -> Error, not Waiting)
    WorkerProfile badBin = p;
    badBin.id = QStringLiteral("ssh_bad_bin");
    badBin.name = QStringLiteral("Bad Bin SSH");
    badBin.remoteBinaryPath = QStringLiteral("/nonexistent/cpuminer");
    controller.addProfile(badBin);
    controller.startWorker(badBin.id);
    QTRY_COMPARE_WITH_TIMEOUT(controller.runtimeById(badBin.id).status, WorkerStatus::Error, 4000);
    controller.removeProfile(badBin.id, false);

    // 8. Missing ssh executable (FailedToStart) -> RpcClient recovers (doesn't lock inflight) & controller sets Error
    qputenv("PATH", emptyBinDir.toUtf8());
    RpcClient rpcNoSsh;
    rpcNoSsh.refreshTargets({p});
    QTRY_VERIFY_WITH_TIMEOUT(rpcNoSsh.statusForTarget(p.targetKey()).checked, 4000);
    QCOMPARE(rpcNoSsh.statusForTarget(p.targetKey()).reachable, false);

    // Restore PATH and verify second refreshTargets succeeds (proving m_inflightTargets wasn't leaked)
    qputenv("PATH", binDir.toUtf8() + ':' + origPath);
    rpcNoSsh.refreshTargets({p});
    QTRY_VERIFY_WITH_TIMEOUT(rpcNoSsh.statusForTarget(p.targetKey()).reachable, 4000);

    qputenv("PATH", emptyBinDir.toUtf8());
    controller.startWorker(p.id);
    QCOMPARE(controller.runtimeById(p.id).status, WorkerStatus::Error);
    QVERIFY(controller.runtimeById(p.id).lastError.contains(QStringLiteral("ssh executable not found")));
#else
    QSKIP("POSIX fake SSH harness test skipped on Windows");
#endif
}

void TestCore::testKristaFairnessRpcAndSeatExpiry()
{
    // 1. Seat expiry & track badge rules aligned to KRISTA v1.5.9.0 / UPGRADE_FAIRNESS
    PoolSeatInfo mnSeat{true, true, QStringLiteral("masternode"), 0, 0, 9000};
    QVERIFY(!mnSeat.hasExpiry());
    QCOMPARE(mnSeat.renewalThresholdBlocks(QStringLiteral("main")), qint64(0));
    QVERIFY(!mnSeat.isExpiringSoon(QStringLiteral("main")));
    QVERIFY(!mnSeat.isRenewableTrack());
    QCOMPARE(formatSeatTrackBadge(mnSeat), QStringLiteral("masternode"));

    PoolSeatInfo bsSeat{true, true, QStringLiteral("bootstrap"), 0, 0, 9000};
    QVERIFY(!bsSeat.hasExpiry());
    QVERIFY(!bsSeat.isExpiringSoon(QStringLiteral("main")));
    QCOMPARE(formatSeatTrackBadge(bsSeat), QStringLiteral("bootstrap"));

    PoolSeatInfo clExpiring{true, true, QStringLiteral("coin_lock"), 9240, 240, 9000};
    QVERIFY(clExpiring.hasExpiry());
    QCOMPARE(clExpiring.renewalThresholdBlocks(QStringLiteral("main")), qint64(240));
    QVERIFY(clExpiring.isExpiringSoon(QStringLiteral("main")));
    QVERIFY(clExpiring.isRenewableTrack());
    QCOMPARE(formatSeatTrackBadge(clExpiring), QStringLiteral("coin_lock (240b)"));

    PoolSeatInfo clHealthy{true, true, QStringLiteral("coin_lock"), 9241, 241, 9000};
    QVERIFY(!clHealthy.isExpiringSoon(QStringLiteral("main")));

    PoolSeatInfo powTest{true, true, QStringLiteral("pow_lock"), 9015, 15, 9000};
    QVERIFY(powTest.isExpiringSoon(QStringLiteral("test")));
    PoolSeatInfo powTestHealthy{true, true, QStringLiteral("pow_lock"), 9016, 16, 9000};
    QVERIFY(!powTestHealthy.isExpiringSoon(QStringLiteral("test")));

    PoolSeatInfo zeroExpiring{true, true, QStringLiteral("zerocoin_pow"), 9024, 24, 9000};
    QVERIFY(zeroExpiring.isExpiringSoon(QStringLiteral("main")));
    const PoolSeatInfo zeroHealthy{true, true, QStringLiteral("zerocoin_pow"), 9025, 25, 9000};
    QVERIFY(!zeroHealthy.isExpiringSoon(QStringLiteral("main")));
    const PoolSeatInfo zeroRegExpiring{true, true, QStringLiteral("zerocoin_pow"), 9008, 8, 9000};
    QVERIFY(zeroRegExpiring.isExpiringSoon(QStringLiteral("regtest")));
    const PoolSeatInfo zeroRegHealthy{true, true, QStringLiteral("zerocoin_pow"), 9009, 9, 9000};
    QVERIFY(!zeroRegHealthy.isExpiringSoon(QStringLiteral("regtest")));

    // 2. Log parser fixes for ccminer election wait, auto-register clear, and locked wallet detection
    WorkerRuntime rt;
    rt.status = WorkerStatus::Solving;
    WorkerController::parseLogLine(
        QStringLiteral("[2026-11-22 10:00:00] ADAM miner: Waiting for election (our miner is not elected for the current block)"),
        rt);
    QCOMPARE(rt.status, WorkerStatus::Waiting);

    WorkerController::parseLogLine(
        QStringLiteral("[2026-11-22 10:00:05] ADAM Auto-Register deferred: Error: Please enter the wallet passphrase with walletpassphrase first."),
        rt);
    QVERIFY(rt.warningDeferred);
    QVERIFY(rt.warningWalletLocked);

    WorkerController::parseLogLine(
        QStringLiteral("[2026-11-22 10:00:10] ADAM Auto-Register: Successfully registered miner! TxID: deadbeef1234"),
        rt);
    QVERIFY(!rt.warningDeferred);
    QVERIFY(!rt.warningWalletLocked);

    rt.warningDeferred = true;
    WorkerController::parseLogLine(
        QStringLiteral("[2026-11-22 10:00:12] ADAM Auto-Register: A PoW-Lock registration is pending in mempool (waiting for block #9001)"),
        rt);
    QVERIFY(!rt.warningDeferred);

    // 3. Mock KRISTA v1.5.9.0 HTTP JSON-RPC node for getwalletinfo, getblockchaininfo, getadamminers, registerminer
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    const quint16 port = server.serverPort();

    enum class WalletMockMode { Locked, Unlocked, Unencrypted };
    WalletMockMode walletMode = WalletMockMode::Locked;
    QList<QPair<QString, QJsonArray>> registerCalls;
    QHash<QTcpSocket*, QByteArray> buffers;

    QObject::connect(&server, &QTcpServer::newConnection, &server, [&]() {
        while (QTcpSocket* sock = server.nextPendingConnection()) {
            QObject::connect(sock, &QTcpSocket::disconnected, sock, [sock, &buffers]() {
                buffers.remove(sock);
                sock->deleteLater();
            });
            QObject::connect(sock, &QTcpSocket::readyRead, sock, [sock, &buffers, &walletMode, &registerCalls]() {
                QByteArray& buf = buffers[sock];
                buf.append(sock->readAll());
                const int hdrEnd = buf.indexOf("\r\n\r\n");
                if (hdrEnd < 0) return;
                const QByteArray hdrs = buf.left(hdrEnd);
                int contentLen = 0;
                for (const QByteArray& line : hdrs.split('\n')) {
                    const QByteArray trimmedLine = line.trimmed();
                    if (trimmedLine.toLower().startsWith("content-length:")) {
                        contentLen = trimmedLine.mid(15).trimmed().toInt();
                    }
                }
                if (buf.size() < hdrEnd + 4 + contentLen) return;
                const QByteArray body = buf.mid(hdrEnd + 4, contentLen);
                buf.clear();

                const QJsonObject reqObj = QJsonDocument::fromJson(body).object();
                const QString method = reqObj.value(QStringLiteral("method")).toString();
                const QJsonArray params = reqObj.value(QStringLiteral("params")).toArray();

                QJsonObject respObj;
                respObj.insert(QStringLiteral("id"), reqObj.value(QStringLiteral("id")));
                respObj.insert(QStringLiteral("error"), QJsonValue::Null);

                if (method == QStringLiteral("getmininginfo")) {
                    QJsonObject res;
                    res.insert(QStringLiteral("blocks"), 9000);
                    res.insert(QStringLiteral("difficulty"), 0.0025);
                    res.insert(QStringLiteral("errors"), QStringLiteral(""));
                    res.insert(QStringLiteral("networkhashps"), 55555.0);
                    res.insert(QStringLiteral("chain"), QStringLiteral("main"));
                    res.insert(QStringLiteral("generate"), false);
                    res.insert(QStringLiteral("genproclimit"), 0);
                    respObj.insert(QStringLiteral("result"), res);
                } else if (method == QStringLiteral("getwalletinfo")) {
                    QJsonObject res;
                    res.insert(QStringLiteral("walletversion"), 15900);
                    res.insert(QStringLiteral("balance"), 500.0);
                    if (walletMode == WalletMockMode::Locked) {
                        res.insert(QStringLiteral("unlocked_until"), 0);
                    } else if (walletMode == WalletMockMode::Unlocked) {
                        res.insert(QStringLiteral("unlocked_until"), 1899999999);
                    }
                    respObj.insert(QStringLiteral("result"), res);
                } else if (method == QStringLiteral("getblockchaininfo")) {
                    QJsonObject upFairness{{QStringLiteral("activationheight"), 1}, {QStringLiteral("status"), QStringLiteral("active")}};
                    QJsonObject upModelD{{QStringLiteral("activationheight"), 1}, {QStringLiteral("status"), QStringLiteral("active")}};
                    QJsonObject upOpen{{QStringLiteral("activationheight"), 2500}, {QStringLiteral("status"), QStringLiteral("active")}};
                    QJsonObject upTreas{{QStringLiteral("activationheight"), 999999}, {QStringLiteral("status"), QStringLiteral("defined")}};
                    QJsonObject upgrades;
                    upgrades.insert(QStringLiteral("Fairness"), upFairness);
                    upgrades.insert(QStringLiteral("Model D"), upModelD);
                    upgrades.insert(QStringLiteral("ADAM Open Entry"), upOpen);
                    upgrades.insert(QStringLiteral("Treasury Governance"), upTreas);

                    QJsonObject res;
                    res.insert(QStringLiteral("chain"), QStringLiteral("main"));
                    res.insert(QStringLiteral("blocks"), 9000);
                    res.insert(QStringLiteral("initialblockdownload"), false);
                    res.insert(QStringLiteral("upgrades"), upgrades);
                    respObj.insert(QStringLiteral("result"), res);
                } else if (method == QStringLiteral("getadamminers")) {
                    QJsonArray arr;
                    arr.append(QJsonObject{{QStringLiteral("address"), QStringLiteral("KTMn000000000000000000000000000001")},
                                           {QStringLiteral("track"), QStringLiteral("masternode")},
                                           {QStringLiteral("unlock_height"), 0}});
                    arr.append(QJsonObject{{QStringLiteral("address"), QStringLiteral("KTCl000000000000000000000000000002")},
                                           {QStringLiteral("track"), QStringLiteral("coin_lock")},
                                           {QStringLiteral("unlock_height"), 9150}});
                    arr.append(QJsonObject{{QStringLiteral("address"), QStringLiteral("KTPw000000000000000000000000000003")},
                                           {QStringLiteral("track"), QStringLiteral("pow_lock")},
                                           {QStringLiteral("unlock_height"), 11000}});
                    arr.append(QJsonObject{{QStringLiteral("address"), QStringLiteral("KTZr000000000000000000000000000004")},
                                           {QStringLiteral("track"), QStringLiteral("zerocoin_pow")},
                                           {QStringLiteral("unlock_height"), 9018}});
                    respObj.insert(QStringLiteral("result"), arr);
                } else if (method == QStringLiteral("registerminer")) {
                    registerCalls.append({params.at(0).toString(), params});
                    respObj.insert(QStringLiteral("result"),
                                   params.at(0).toString() == QStringLiteral("lock")
                                       ? QStringLiteral("tx_renew_lock_001")
                                       : QStringLiteral("tx_renew_pow_002"));
                }

                const QByteArray payload = QJsonDocument(respObj).toJson(QJsonDocument::Compact);
                const QByteArray resp =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json\r\n"
                    "Content-Length: " + QByteArray::number(payload.size()) + "\r\n"
                    "Connection: close\r\n\r\n" + payload;
                sock->write(resp);
                sock->disconnectFromHost();
            });
        }
    });

    RpcClient rpc;
    WorkerProfile p;
    p.id = QStringLiteral("fairness_rpc");
    p.name = QStringLiteral("Fairness RPC");
    p.targetType = TargetType::CustomRpc;
    p.rpcUrl = QStringLiteral("127.0.0.1:%1").arg(port);
    p.rpcUser = QStringLiteral("u");
    p.rpcPassword = QStringLiteral("p");
    p.coinbaseAddress = QStringLiteral("KTCl000000000000000000000000000002");

    rpc.refreshTargets({p});
    QTRY_VERIFY_WITH_TIMEOUT(
        rpc.statusForTarget(p.targetKey()).walletChecked
        && rpc.consensusStatus().known
        && rpc.allKnownSeats().size() == 4,
        4000);

    NodeMiningStatus st = rpc.statusForTarget(p.targetKey());
    QCOMPARE(st.chain, QStringLiteral("main"));
    QVERIFY(st.walletEncrypted);
    QVERIFY(st.walletLocked);
    QCOMPARE(st.walletUnlockedUntil, qint64(0));

    const ConsensusStatus cs = rpc.consensusStatus();
    QVERIFY(cs.fairnessActive);
    QVERIFY(cs.modelDActive);
    QVERIFY(cs.openEntryActive);
    QCOMPARE(cs.badgeLabel(), QStringLiteral("Fairness"));
    QVERIFY(cs.activeUpgrades.contains(QStringLiteral("Fairness")));
    QVERIFY(!cs.activeUpgrades.contains(QStringLiteral("Treasury Governance")));

    QVERIFY(!rpc.seatInfoForAddress(QStringLiteral("KTMn000000000000000000000000000001")).isExpiringSoon(st.chain));
    QVERIFY(rpc.seatInfoForAddress(QStringLiteral("KTCl000000000000000000000000000002")).isExpiringSoon(st.chain));
    QVERIFY(!rpc.seatInfoForAddress(QStringLiteral("KTPw000000000000000000000000000003")).isExpiringSoon(st.chain));
    QVERIFY(rpc.seatInfoForAddress(QStringLiteral("KTZr000000000000000000000000000004")).isExpiringSoon(st.chain));

    // Flip to Unlocked
    walletMode = WalletMockMode::Unlocked;
    rpc.refreshTargets({p});
    QTRY_VERIFY_WITH_TIMEOUT(!rpc.statusForTarget(p.targetKey()).walletLocked, 4000);
    st = rpc.statusForTarget(p.targetKey());
    QVERIFY(st.walletEncrypted);
    QVERIFY(!st.walletLocked);
    QCOMPARE(st.walletUnlockedUntil, qint64(1899999999));

    // Flip to Unencrypted
    walletMode = WalletMockMode::Unencrypted;
    rpc.refreshTargets({p});
    QTRY_VERIFY_WITH_TIMEOUT(!rpc.statusForTarget(p.targetKey()).walletEncrypted, 4000);
    st = rpc.statusForTarget(p.targetKey());
    QVERIFY(st.walletChecked);
    QVERIFY(!st.walletEncrypted);
    QVERIFY(!st.walletLocked);

    // Seat renewal RPC parameter verification
    bool lockDone = false, lockOk = false;
    QString lockTx;
    rpc.renewMinerSeat(p, QStringLiteral("coin_lock"), QStringLiteral("KTCl000000000000000000000000000002"), 9000,
                       [&](bool ok, const QString& tx) {
                           lockDone = true;
                           lockOk = ok;
                           lockTx = tx;
                       });
    QTRY_VERIFY_WITH_TIMEOUT(lockDone, 4000);
    QVERIFY(lockOk);
    QCOMPARE(lockTx, QStringLiteral("tx_renew_lock_001"));

    bool powDone = false, powOk = false;
    QString powTx;
    rpc.renewMinerSeat(p, QStringLiteral("pow_lock"), QStringLiteral("KTPw000000000000000000000000000003"), 9000,
                       [&](bool ok, const QString& tx) {
                           powDone = true;
                           powOk = ok;
                           powTx = tx;
                       });
    QTRY_VERIFY_WITH_TIMEOUT(powDone, 4000);
    QVERIFY(powOk);
    QCOMPARE(powTx, QStringLiteral("tx_renew_pow_002"));

    QCOMPARE(registerCalls.size(), 2);
    QCOMPARE(registerCalls[0].second.at(0).toString(), QStringLiteral("lock"));
    QCOMPARE(static_cast<qint64>(registerCalls[0].second.at(1).toDouble()), qint64(11900));
    QCOMPARE(registerCalls[0].second.at(2).toString(), QStringLiteral("KTCl000000000000000000000000000002"));
    QCOMPARE(registerCalls[1].second.at(0).toString(), QStringLiteral("pow"));
    QCOMPARE(registerCalls[1].second.at(1).toString(), QStringLiteral(""));
    QCOMPARE(registerCalls[1].second.at(2).toString(), QStringLiteral("KTPw000000000000000000000000000003"));
}

void TestCore::testSecurityHardening()
{
    // 1. Podman name & expected miner binary validation
    QVERIFY(isValidPodmanName(QStringLiteral("krista-cold-01")));
    QVERIFY(isValidPodmanName(QStringLiteral("node_2.test")));
    QVERIFY(!isValidPodmanName(QString()));
    QVERIFY(!isValidPodmanName(QStringLiteral("-n")));
    QVERIFY(!isValidPodmanName(QStringLiteral("--privileged")));
    QVERIFY(!isValidPodmanName(QStringLiteral("pod;rm -rf /")));
    QVERIFY(!isValidPodmanName(QStringLiteral("../escape")));

    QVERIFY(isExpectedMinerBinaryPath(QStringLiteral("/usr/local/bin/cpuminer")));
    QVERIFY(isExpectedMinerBinaryPath(QStringLiteral("cpuminer-opt")));
    QVERIFY(isExpectedMinerBinaryPath(QStringLiteral("C:\\miners\\ccminer.exe")));
    QVERIFY(!isExpectedMinerBinaryPath(QString()));
    QVERIFY(!isExpectedMinerBinaryPath(QStringLiteral("/bin/sh")));
    QVERIFY(!isExpectedMinerBinaryPath(QStringLiteral("C:\\Windows\\System32\\cmd.exe")));

    // 2. WorkerProfile::fromJson strips unsafe Podman/SSH target fields
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), QStringLiteral("pod_bad"));
        obj.insert(QStringLiteral("name"), QStringLiteral("Bad Pod"));
        obj.insert(QStringLiteral("targetType"), QStringLiteral("podman"));
        obj.insert(QStringLiteral("podmanName"), QStringLiteral("--privileged"));
        const WorkerProfile p = WorkerProfile::fromJson(obj);
        QVERIFY(p.podmanName.isEmpty());
    }
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), QStringLiteral("ssh_bad"));
        obj.insert(QStringLiteral("name"), QStringLiteral("Bad SSH"));
        obj.insert(QStringLiteral("targetType"), QStringLiteral("ssh"));
        obj.insert(QStringLiteral("sshTarget"), QStringLiteral("-oProxyCommand=evil"));
        const WorkerProfile p = WorkerProfile::fromJson(obj);
        QVERIFY(p.sshTarget.isEmpty());
    }

    // 3. ConfigStore & RpcClient reject invalid podmanName cleanly
    WorkerProfile badPod;
    badPod.id = QStringLiteral("bad_pod");
    badPod.name = QStringLiteral("Bad Pod");
    badPod.targetType = TargetType::PodmanContainer;
    badPod.podmanName = QStringLiteral("--privileged");
    const RpcCredentials podCreds = ConfigStore::resolveCredentials(badPod);
    QVERIFY(!podCreds.valid);
    QVERIFY(podCreds.error.contains(QStringLiteral("Invalid Podman container name")));

    RpcClient rpc;
    bool podRpcDone = false, podRpcOk = true;
    QString podRpcErr;
    rpc.setInternalGenerate(badPod, false, [&](bool ok, const QString& err) {
        podRpcDone = true;
        podRpcOk = ok;
        podRpcErr = err;
    });
    QTRY_VERIFY_WITH_TIMEOUT(podRpcDone, 2000);
    QVERIFY(!podRpcOk);
    QVERIFY(podRpcErr.contains(QStringLiteral("Invalid Podman container name")));

    // 4. Expanded credential redaction in logs
    const QString rawSecrets = QStringLiteral(
        "Authorization: Basic dXNlcjpzZWNyZXQ= | Authorization: Bearer mySecretJwt123 | "
        "cmd --user alice --pass hunter2 -u bob -p p@ssw0rd");
    const QString masked = ConfigStore::sanitizeLogLine(rawSecrets);
    QVERIFY(!masked.contains(QStringLiteral("dXNlcjpzZWNyZXQ=")));
    QVERIFY(!masked.contains(QStringLiteral("mySecretJwt123")));
    QVERIFY(!masked.contains(QStringLiteral("alice")));
    QVERIFY(!masked.contains(QStringLiteral("hunter2")));
    QVERIFY(!masked.contains(QStringLiteral("bob")));
    QVERIFY(!masked.contains(QStringLiteral("p@ssw0rd")));
    QVERIFY(masked.contains(QStringLiteral("Authorization: Basic ***")));
    QVERIFY(masked.contains(QStringLiteral("Authorization: Bearer ***")));

    // 5. Import security inspection & sanitization
    WorkerProfile riskyLocal;
    riskyLocal.id = QStringLiteral("risk_local");
    riskyLocal.name = QStringLiteral("Risky Local");
    riskyLocal.targetType = TargetType::LocalWallet;
    riskyLocal.binaryPath = QStringLiteral("/bin/sh");
    riskyLocal.confPath = QStringLiteral("/tmp/evil.conf");
    riskyLocal.extraArgs = QStringLiteral("--api-remote");

    WorkerProfile riskyHttp;
    riskyHttp.id = QStringLiteral("risk_http");
    riskyHttp.name = QStringLiteral("Risky HTTP");
    riskyHttp.targetType = TargetType::CustomRpc;
    riskyHttp.rpcUrl = QStringLiteral("http://198.51.100.10:27998/");

    QVector<WorkerProfile> imported{riskyLocal, riskyHttp};
    const ImportSecurityReport rep = ConfigStore::inspectImportedProfiles(imported);
    QVERIFY(rep.hasRiskyFields());
    QCOMPARE(rep.customBinaryCount, 1);
    QCOMPARE(rep.extraArgsCount, 1);
    QCOMPARE(rep.customConfCount, 1);
    QCOMPARE(rep.plaintextRemoteHttpCount, 1);

    ConfigStore::sanitizeImportedProfiles(imported);
    QVERIFY(imported[0].binaryPath.isEmpty());
    QVERIFY(imported[0].extraArgs.isEmpty());
    QCOMPARE(imported[0].confPath, ConfigStore::defaultKristaTechConfPath());

    // 6. HTTP JSON-RPC 16 MiB response cap aborts cleanly
    QTcpServer hugeServer;
    QVERIFY(hugeServer.listen(QHostAddress::LocalHost, 0));
    QObject::connect(&hugeServer, &QTcpServer::newConnection, &hugeServer, [&]() {
        while (QTcpSocket* sock = hugeServer.nextPendingConnection()) {
            QObject::connect(sock, &QTcpSocket::readyRead, sock, [sock]() {
                sock->readAll();
                const QByteArray hdr =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json\r\n"
                    "Content-Length: 20000000\r\n"
                    "Connection: close\r\n\r\n{";
                sock->write(hdr);
                sock->flush();
            });
            QObject::connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
        }
    });

    WorkerProfile capProf;
    capProf.id = QStringLiteral("cap_rpc");
    capProf.name = QStringLiteral("Cap RPC");
    capProf.targetType = TargetType::CustomRpc;
    capProf.rpcUrl = QStringLiteral("127.0.0.1:%1").arg(hugeServer.serverPort());
    capProf.rpcUser = QStringLiteral("u");
    capProf.rpcPassword = QStringLiteral("p");

    bool capDone = false, capOk = true;
    QString capErr;
    rpc.setInternalGenerate(capProf, false, [&](bool ok, const QString& err) {
        capDone = true;
        capOk = ok;
        capErr = err;
    });
    QTRY_VERIFY_WITH_TIMEOUT(capDone, 4000);
    QVERIFY(!capOk);
    QVERIFY(capErr.contains(QStringLiteral("16 MiB")));

    // 7. Orphaned run/*.json cleanup removes stopped configs while leaving directories 0700
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    ConfigStore store(tmpDir.filePath(QStringLiteral("sec_cfg")));
    QVERIFY(store.ensureDirectories());
    QVERIFY(ConfigStore::writeFile0600(store.runtimeJsonPath(QStringLiteral("orphan_1")), QByteArray("{}")));

    WorkerProfile stoppedProf;
    stoppedProf.id = QStringLiteral("stopped_prof");
    stoppedProf.name = QStringLiteral("Stopped Prof");
    stoppedProf.targetType = TargetType::LocalWallet;
    QVERIFY(store.saveProfiles({stoppedProf}));
    QVERIFY(ConfigStore::writeFile0600(store.runtimeJsonPath(stoppedProf.id), QByteArray("{\"secret\":\"x\"}")));

    RpcClient localRpc;
    WorkerController ctrl(&store, &localRpc);
    ctrl.loadInitialState(/*autoDiscoverIfEmpty=*/false);
    QVERIFY(!QFileInfo::exists(store.runtimeJsonPath(QStringLiteral("orphan_1"))));
    QVERIFY(!QFileInfo::exists(store.runtimeJsonPath(stoppedProf.id)));

    // 8. PID identity verification rejects unrelated current process
    QVERIFY(!ProcessScanner::isManagedMinerOrWrapperPid(QCoreApplication::applicationPid()));

#ifndef Q_OS_WIN
    // 9. Bounded SSH log tail & corrupted .pid guard on POSIX
    const QString binDir = tmpDir.filePath(QStringLiteral("bin"));
    const QString remoteHome = tmpDir.filePath(QStringLiteral("remote_home"));
    QVERIFY(QDir().mkpath(binDir));
    QVERIFY(QDir().mkpath(remoteHome + QStringLiteral("/.kristatech-mm")));

    const QString fakeSsh = binDir + QStringLiteral("/ssh");
    {
        QFile f(fakeSsh);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray script = QStringLiteral(
            "#!/bin/sh\n"
            "REMOTE_HOME=\"%1\"\n"
            "SAW_NOTTY=0\n"
            "SAW_CLEARFWD=0\n"
            "SAW_DD=0\n"
            "TARGET=\"\"\n"
            "CMD=\"\"\n"
            "while [ \"$#\" -gt 0 ]; do\n"
            "  if [ \"$SAW_DD\" -eq 1 ]; then TARGET=\"$1\"; shift; CMD=\"$*\"; break; fi\n"
            "  case \"$1\" in\n"
            "    -o)\n"
            "      if [ \"$2\" = \"RequestTTY=no\" ]; then SAW_NOTTY=1; fi\n"
            "      if [ \"$2\" = \"ClearAllForwardings=yes\" ]; then SAW_CLEARFWD=1; fi\n"
            "      shift 2 ;;\n"
            "    -p) shift 2 ;;\n"
            "    --) SAW_DD=1; shift ;;\n"
            "    *) TARGET=\"$1\"; shift; CMD=\"$*\"; break ;;\n"
            "  esac\n"
            "done\n"
            "if [ \"$SAW_NOTTY\" -ne 1 ] || [ \"$SAW_CLEARFWD\" -ne 1 ]; then exit 97; fi\n"
            "exec env HOME=\"$REMOTE_HOME\" /bin/sh -c \"$CMD\"\n")
            .arg(remoteHome).toUtf8();
        f.write(script);
        f.close();
        QVERIFY(f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    }

    const QByteArray origPath = qgetenv("PATH");
    struct PathGuard {
        QByteArray orig;
        ~PathGuard() { qputenv("PATH", orig); }
    } pathGuard{origPath};
    qputenv("PATH", binDir.toUtf8() + ':' + origPath);

    // Corrupted .pid with "-1" must be treated as DEAD by checkRemoteSshHealthNow without calling kill
    WorkerProfile sshP;
    sshP.id = QStringLiteral("ssh_sec");
    sshP.name = QStringLiteral("SSH Sec");
    sshP.targetType = TargetType::RemoteSsh;
    sshP.sshTarget = QStringLiteral("miner@192.0.2.10");
    sshP.rpcUrl = QStringLiteral("127.0.0.1:27998");
    sshP.rpcUser = QStringLiteral("u");
    sshP.rpcPassword = QStringLiteral("p");
    sshP.autoRestart = false;
    sshP.remoteBinaryPath = QStringLiteral("/usr/bin/printf");
    ctrl.addProfile(sshP);

    // Test bounded 64 KiB tail buffer when fake ssh emits >100 KiB without newline
    {
        QFile fakeShTail(binDir + QStringLiteral("/ssh"));
        QVERIFY(fakeShTail.open(QIODevice::WriteOnly | QIODevice::Truncate));
        fakeShTail.write(QStringLiteral(
            "#!/bin/sh\n"
            "CMD=\"\"; SAW_DD=0\n"
            "while [ \"$#\" -gt 0 ]; do\n"
            "  if [ \"$SAW_DD\" -eq 1 ]; then shift; CMD=\"$*\"; break; fi\n"
            "  case \"$1\" in -o|-p) shift 2 ;; --) SAW_DD=1; shift ;; *) shift; CMD=\"$*\"; break ;; esac\n"
            "done\n"
            "case \"$CMD\" in\n"
            "  *\"umask 077\"*) cat > /dev/null; exit 0 ;;\n"
            "  *\"nohup \"*) sleep 30 & echo $!; exit 0 ;;\n"
            "  *\"tail \"*) head -c 102400 /dev/zero | tr '\\0' 'A'; exec sleep 5 ;;\n"
            "  *\"~/.kristatech-mm/ssh_sec.pid\"*) exec env HOME=\"%1\" /bin/sh -c \"$CMD\" ;;\n"
            "esac\n"
            "exit 0\n").arg(remoteHome).toUtf8());
        fakeShTail.close();
        QVERIFY(fakeShTail.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    }

    ctrl.startWorker(sshP.id);
    QTRY_VERIFY_WITH_TIMEOUT(
        !ctrl.runtimeById(sshP.id).logLines.isEmpty()
        && ctrl.runtimeById(sshP.id).logLines.last().contains(QStringLiteral("[truncated]")),
        4000);
    QVERIFY(ctrl.runtimeById(sshP.id).logLines.last().size() <= 66000);

    // Write corrupted "-1" into remote .pid and run checkRemoteSshHealthNow -> must transition cleanly to Error ("DEAD")
    {
        QFile pidF(remoteHome + QStringLiteral("/.kristatech-mm/ssh_sec.pid"));
        QVERIFY(pidF.open(QIODevice::WriteOnly | QIODevice::Truncate));
        pidF.write("-1\n");
        pidF.close();
    }
    ctrl.checkRemoteSshHealthNow();
    QTRY_COMPARE_WITH_TIMEOUT(ctrl.runtimeById(sshP.id).status, WorkerStatus::Error, 4000);
    ctrl.stopWorker(sshP.id);
    QTest::qWait(100);
#endif
}

QTEST_MAIN(TestCore)
#include "test_core.moc"
