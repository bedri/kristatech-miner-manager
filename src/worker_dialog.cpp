#include "worker_dialog.h"

#include "config_store.h"
#include "i18n.h"
#include "process_scanner.h"
#include "rpc_client.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

WorkerDialog::WorkerDialog(const WorkerProfile* existingProfile,
                           RpcClient* rpc,
                           const QVector<PodmanContainerInfo>& pods,
                           const QVector<GpuTelemetry>& gpus,
                           QWidget* parent)
    : QDialog(parent)
    , m_rpc(rpc)
    , m_pods(pods)
    , m_gpus(gpus)
    , m_isEdit(existingProfile != nullptr)
{
    if (existingProfile) {
        m_original = *existingProfile;
    } else {
        m_original.engine = MinerEngine::CpuMiner;
        m_original.binaryPath = ProcessScanner::defaultCpuMinerPath();
        m_original.targetType = TargetType::LocalWallet;
        m_original.confPath = ConfigStore::defaultKristaTechConfPath();
        m_original.threads = 1;
        m_original.sshPort = 22;
        m_original.autoRestart = true;
    }

    setWindowTitle(KTr(m_isEdit ? QStringLiteral("dlg.edit_title") : QStringLiteral("dlg.add_title")));
    setMinimumWidth(620);

    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 16);
    root->setSpacing(12);

    QFormLayout* form = new QFormLayout();
    m_form = form;
    form->setSpacing(10);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_editName = new QLineEdit(m_original.name, this);
    m_editName->setPlaceholderText(QStringLiteral("worker_gpu_0 / worker_00_local / pod01"));
    form->addRow(KTr(QStringLiteral("dlg.name")), m_editName);

    m_editId = new QLineEdit(m_original.id, this);
    m_editId->setReadOnly(m_isEdit);
    m_editId->setPlaceholderText(QStringLiteral("auto-generated if empty (alphanumeric, _, -)"));
    if (m_isEdit) {
        m_editId->setEnabled(false);
    }
    form->addRow(KTr(QStringLiteral("dlg.id")), m_editId);

    m_comboEngine = new QComboBox(this);
    m_comboEngine->addItem(QStringLiteral("cpuminer (CPU - ADAM)"), static_cast<int>(MinerEngine::CpuMiner));
    m_comboEngine->addItem(QStringLiteral("ccminer (CUDA GPU - ADAM)"), static_cast<int>(MinerEngine::CcMiner));
    m_comboEngine->setCurrentIndex(m_original.engine == MinerEngine::CcMiner ? 1 : 0);
    form->addRow(KTr(QStringLiteral("dlg.engine")), m_comboEngine);

    // Binary row
    QWidget* binRow = new QWidget(this);
    m_rowBinary = binRow;
    QHBoxLayout* binLay = new QHBoxLayout(binRow);
    binLay->setContentsMargins(0, 0, 0, 0);
    binLay->setSpacing(6);
    m_editBinary = new QLineEdit(m_original.binaryPath, binRow);
    m_btnBrowseBin = new QPushButton(KTr(QStringLiteral("dlg.browse")), binRow);
    m_btnTestBin = new QPushButton(KTr(QStringLiteral("dlg.test_bin")), binRow);
    binLay->addWidget(m_editBinary, 1);
    binLay->addWidget(m_btnBrowseBin);
    binLay->addWidget(m_btnTestBin);
    form->addRow(KTr(QStringLiteral("dlg.binary")), binRow);

    // Target Type
    m_comboTarget = new QComboBox(this);
    m_comboTarget->addItem(KTr(QStringLiteral("target.local")), static_cast<int>(TargetType::LocalWallet));
    m_comboTarget->addItem(KTr(QStringLiteral("target.podman")), static_cast<int>(TargetType::PodmanContainer));
    m_comboTarget->addItem(KTr(QStringLiteral("target.custom")), static_cast<int>(TargetType::CustomRpc));
    m_comboTarget->addItem(KTr(QStringLiteral("target.ssh")), static_cast<int>(TargetType::RemoteSsh));
    const int tIdx = m_comboTarget->findData(static_cast<int>(m_original.targetType));
    m_comboTarget->setCurrentIndex(tIdx >= 0 ? tIdx : 0);
    form->addRow(KTr(QStringLiteral("dlg.target_type")), m_comboTarget);

    // Local conf row
    m_rowConf = new QWidget(this);
    QHBoxLayout* confLay = new QHBoxLayout(m_rowConf);
    confLay->setContentsMargins(0, 0, 0, 0);
    confLay->setSpacing(6);
    m_editConfPath = new QLineEdit(
        m_original.confPath.isEmpty() ? ConfigStore::defaultKristaTechConfPath() : m_original.confPath,
        m_rowConf);
    QPushButton* btnBrowseConf = new QPushButton(KTr(QStringLiteral("dlg.browse")), m_rowConf);
    connect(btnBrowseConf, &QPushButton::clicked, this, &WorkerDialog::onBrowseConf);
    confLay->addWidget(m_editConfPath, 1);
    confLay->addWidget(btnBrowseConf);
    form->addRow(KTr(QStringLiteral("dlg.conf_path")), m_rowConf);

    // Podman row
    m_rowPodman = new QWidget(this);
    QHBoxLayout* podLay = new QHBoxLayout(m_rowPodman);
    podLay->setContentsMargins(0, 0, 0, 0);
    m_comboPodman = new QComboBox(m_rowPodman);
    m_comboPodman->setEditable(true);
    for (const PodmanContainerInfo& pod : m_pods) {
        m_comboPodman->addItem(pod.name);
    }
    if (!m_original.podmanName.isEmpty()) {
        const int pIdx = m_comboPodman->findText(m_original.podmanName);
        if (pIdx >= 0) m_comboPodman->setCurrentIndex(pIdx);
        else m_comboPodman->setCurrentText(m_original.podmanName);
    }
    podLay->addWidget(m_comboPodman);
    form->addRow(KTr(QStringLiteral("dlg.podman_name")), m_rowPodman);

    // Custom / SSH RPC URL row
    m_rowRpcUrl = new QWidget(this);
    QHBoxLayout* urlLay = new QHBoxLayout(m_rowRpcUrl);
    urlLay->setContentsMargins(0, 0, 0, 0);
    m_editRpcUrl = new QLineEdit(
        m_original.rpcUrl.isEmpty() ? QStringLiteral("http://127.0.0.1:27998/") : m_original.rpcUrl,
        m_rowRpcUrl);
    urlLay->addWidget(m_editRpcUrl);
    form->addRow(KTr(QStringLiteral("dlg.rpc_url")), m_rowRpcUrl);

    // Custom / SSH RPC Auth row
    m_rowRpcAuth = new QWidget(this);
    QHBoxLayout* authLay = new QHBoxLayout(m_rowRpcAuth);
    authLay->setContentsMargins(0, 0, 0, 0);
    authLay->setSpacing(8);
    m_editRpcUser = new QLineEdit(m_original.rpcUser, m_rowRpcAuth);
    m_editRpcUser->setPlaceholderText(KTr(QStringLiteral("dlg.rpc_user")));
    m_editRpcPass = new QLineEdit(m_original.rpcPassword, m_rowRpcAuth);
    m_editRpcPass->setEchoMode(QLineEdit::Password);
    m_editRpcPass->setPlaceholderText(KTr(QStringLiteral("dlg.rpc_pass")));
    authLay->addWidget(m_editRpcUser, 1);
    authLay->addWidget(m_editRpcPass, 1);
    form->addRow(KTr(QStringLiteral("dlg.rpc_user")), m_rowRpcAuth);

    // Remote SSH fields
    m_rowSsh = new QWidget(this);
    QVBoxLayout* sshLay = new QVBoxLayout(m_rowSsh);
    sshLay->setContentsMargins(0, 0, 0, 0);
    sshLay->setSpacing(6);
    QHBoxLayout* sshTop = new QHBoxLayout();
    sshTop->setSpacing(6);
    m_editSshTarget = new QLineEdit(m_original.sshTarget, m_rowSsh);
    m_editSshTarget->setPlaceholderText(QStringLiteral("user@server-ip"));
    m_spinSshPort = new QSpinBox(m_rowSsh);
    m_spinSshPort->setRange(1, 65535);
    m_spinSshPort->setValue(m_original.sshPort > 0 ? m_original.sshPort : 22);
    m_spinSshPort->setPrefix(QStringLiteral("Port: "));
    sshTop->addWidget(m_editSshTarget, 1);
    sshTop->addWidget(m_spinSshPort);
    m_editRemoteBin = new QLineEdit(m_original.remoteBinaryPath, m_rowSsh);
    m_editRemoteBin->setPlaceholderText(QStringLiteral("/usr/local/bin/cpuminer (or ccminer)"));
    sshLay->addLayout(sshTop);
    sshLay->addWidget(m_editRemoteBin);
    form->addRow(KTr(QStringLiteral("dlg.ssh_target")), m_rowSsh);

    // Coinbase Address
    m_comboAddress = new QComboBox(this);
    m_comboAddress->setEditable(true);
    populateAddresses();
    if (!m_original.coinbaseAddress.isEmpty()) {
        m_comboAddress->setEditText(m_original.coinbaseAddress);
    }
    form->addRow(KTr(QStringLiteral("dlg.address")), m_comboAddress);

    root->addLayout(form);

    // Inline Pool Registration Warning Banner
    m_warnFrame = new QFrame(this);
    m_warnFrame->setObjectName(QStringLiteral("warningBanner"));
    QHBoxLayout* warnLay = new QHBoxLayout(m_warnFrame);
    warnLay->setContentsMargins(10, 8, 10, 8);
    m_warnLabel = new QLabel(KTr(QStringLiteral("dlg.unreg_warn")), m_warnFrame);
    m_warnLabel->setObjectName(QStringLiteral("warnLabel"));
    m_warnLabel->setWordWrap(true);
    warnLay->addWidget(m_warnLabel);
    m_warnFrame->setVisible(false);
    root->addWidget(m_warnFrame);

    // CPU Hardware section
    m_cpuSection = new QWidget(this);
    QFormLayout* cpuForm = new QFormLayout(m_cpuSection);
    cpuForm->setContentsMargins(0, 0, 0, 0);
    cpuForm->setSpacing(8);
    cpuForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_spinThreads = new QSpinBox(m_cpuSection);
    m_spinThreads->setRange(1, 256);
    m_spinThreads->setValue(qMax(1, m_original.threads));
    cpuForm->addRow(KTr(QStringLiteral("dlg.threads")), m_spinThreads);

    m_editAffinity = new QLineEdit(m_original.cpuAffinity, m_cpuSection);
    m_editAffinity->setPlaceholderText(QStringLiteral("e.g. 0 or 0-3 or 2,4 (optional)"));
    cpuForm->addRow(KTr(QStringLiteral("dlg.affinity")), m_editAffinity);

    m_spinMaxTemp = new QSpinBox(m_cpuSection);
    m_spinMaxTemp->setRange(0, 110);
    m_spinMaxTemp->setValue(qMax(0, m_original.maxTemp));
    cpuForm->addRow(KTr(QStringLiteral("dlg.max_temp")), m_spinMaxTemp);
    root->addWidget(m_cpuSection);

    // GPU Hardware section
    m_gpuSection = new QWidget(this);
    QFormLayout* gpuForm = new QFormLayout(m_gpuSection);
    gpuForm->setContentsMargins(0, 0, 0, 0);
    gpuForm->setSpacing(8);
    gpuForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_comboGpuDevices = new QComboBox(m_gpuSection);
    m_comboGpuDevices->setEditable(true);
    if (!m_gpus.isEmpty()) {
        for (const GpuTelemetry& g : m_gpus) {
            m_comboGpuDevices->addItem(QStringLiteral("%1 (%2)").arg(g.index).arg(g.name),
                                       QString::number(g.index));
        }
        if (m_gpus.size() > 1) {
            QStringList idxs;
            for (const GpuTelemetry& g : m_gpus) idxs << QString::number(g.index);
            m_comboGpuDevices->addItem(QStringLiteral("All GPUs (%1)").arg(idxs.join(',')), idxs.join(','));
        }
    } else {
        m_comboGpuDevices->addItem(QStringLiteral("0"), QStringLiteral("0"));
    }
    if (!m_original.gpuDevices.isEmpty()) {
        m_comboGpuDevices->setEditText(m_original.gpuDevices);
    }
    gpuForm->addRow(KTr(QStringLiteral("dlg.gpu_devices")), m_comboGpuDevices);

    m_spinGpuIntensity = new QDoubleSpinBox(m_gpuSection);
    m_spinGpuIntensity->setRange(0.0, 31.0);
    m_spinGpuIntensity->setSingleStep(0.5);
    m_spinGpuIntensity->setDecimals(1);
    m_spinGpuIntensity->setValue(m_original.gpuIntensity);
    gpuForm->addRow(KTr(QStringLiteral("dlg.gpu_intensity")), m_spinGpuIntensity);
    root->addWidget(m_gpuSection);

    // Extra & behavior options
    QFormLayout* miscForm = new QFormLayout();
    miscForm->setSpacing(8);
    miscForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_editExtraArgs = new QLineEdit(m_original.extraArgs, this);
    m_editExtraArgs->setPlaceholderText(QStringLiteral("optional extra flags (e.g. -q)"));
    miscForm->addRow(KTr(QStringLiteral("dlg.extra_args")), m_editExtraArgs);
    root->addLayout(miscForm);

    m_chkAutoRestart = new QCheckBox(KTr(QStringLiteral("dlg.auto_restart")), this);
    m_chkAutoRestart->setChecked(m_original.autoRestart);
    root->addWidget(m_chkAutoRestart);

    m_chkDisableInternalGen = new QCheckBox(KTr(QStringLiteral("dlg.disable_gen_start")), this);
    m_chkDisableInternalGen->setChecked(m_original.disableInternalGenOnStart);
    root->addWidget(m_chkDisableInternalGen);

    // Bottom buttons
    QHBoxLayout* btnRow = new QHBoxLayout();
    btnRow->addStretch(1);
    QPushButton* btnCancel = new QPushButton(KTr(QStringLiteral("dlg.cancel")), this);
    QPushButton* btnSave = new QPushButton(KTr(QStringLiteral("dlg.save")), this);
    btnSave->setProperty("variant", QStringLiteral("primary"));
    btnRow->addWidget(btnCancel);
    btnRow->addWidget(btnSave);
    root->addLayout(btnRow);

    connect(m_comboEngine, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &WorkerDialog::onEngineChanged);
    connect(m_comboTarget, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &WorkerDialog::onTargetTypeChanged);
    connect(m_comboPodman, &QComboBox::currentTextChanged, this, &WorkerDialog::onPodmanSelectionChanged);
    connect(m_comboAddress, &QComboBox::currentTextChanged, this, &WorkerDialog::onAddressChanged);
    connect(m_btnBrowseBin, &QPushButton::clicked, this, &WorkerDialog::onBrowseBinary);
    connect(m_btnTestBin, &QPushButton::clicked, this, &WorkerDialog::onTestBinary);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(btnSave, &QPushButton::clicked, this, &WorkerDialog::onAcceptClicked);

    updateVisibilityForEngineAndTarget();
    validateAddressWarning();
}

void WorkerDialog::populateAddresses()
{
    if (!m_rpc) return;
    const auto localSeats = m_rpc->localOwnedSeats();
    for (const auto& pair : localSeats) {
        const QString label = QStringLiteral("%1  [Wallet: %2, %3b left]")
            .arg(pair.first, pair.second.track)
            .arg(pair.second.remainingBlocks);
        m_comboAddress->addItem(label, pair.first);
    }
    const auto allSeats = m_rpc->allKnownSeats();
    for (auto it = allSeats.constBegin(); it != allSeats.constEnd(); ++it) {
        if (m_comboAddress->findData(it.key()) >= 0) continue;
        const QString label = QStringLiteral("%1  [%2]")
            .arg(it.key(), it.value().track);
        m_comboAddress->addItem(label, it.key());
    }
}

QString WorkerDialog::extractedCoinbaseAddress() const
{
    const int curIdx = m_comboAddress->currentIndex();
    const QString text = m_comboAddress->currentText().trimmed();
    if (curIdx >= 0 && m_comboAddress->itemText(curIdx) == text) {
        const QString dataAddr = m_comboAddress->itemData(curIdx).toString().trimmed();
        if (!dataAddr.isEmpty()) return dataAddr;
    }
    const int spacePos = text.indexOf(QLatin1Char(' '));
    return (spacePos > 0) ? text.left(spacePos).trimmed() : text;
}

void WorkerDialog::onEngineChanged(int)
{
    const MinerEngine eng = static_cast<MinerEngine>(m_comboEngine->currentData().toInt());
    const QString curBin = m_editBinary->text().trimmed();
    const QString defCpu = ProcessScanner::defaultCpuMinerPath();
    const QString defGpu = ProcessScanner::defaultCcMinerPath();
    if (!m_suppressBinaryAutoSwitch && (curBin.isEmpty() || curBin == defCpu || curBin == defGpu)) {
        m_editBinary->setText(eng == MinerEngine::CcMiner ? defGpu : defCpu);
    }
    updateVisibilityForEngineAndTarget();
}

void WorkerDialog::onTargetTypeChanged(int)
{
    updateVisibilityForEngineAndTarget();
}

void WorkerDialog::onPodmanSelectionChanged(const QString& podName)
{
    if (static_cast<TargetType>(m_comboTarget->currentData().toInt()) != TargetType::PodmanContainer) {
        return;
    }
    if (!m_comboAddress->currentText().trimmed().isEmpty()) {
        return;
    }
    for (const PodmanContainerInfo& pod : m_pods) {
        if (pod.name == podName.trimmed() && !pod.assignedAddress.isEmpty()) {
            m_comboAddress->setEditText(pod.assignedAddress);
            break;
        }
    }
}

void WorkerDialog::onAddressChanged(const QString&)
{
    validateAddressWarning();
}

void WorkerDialog::validateAddressWarning()
{
    if (!m_rpc) {
        m_warnFrame->setVisible(false);
        return;
    }
    const QString addr = extractedCoinbaseAddress();
    if (addr.isEmpty()) {
        m_warnFrame->setVisible(false);
        return;
    }
    const PoolSeatInfo seat = m_rpc->seatInfoForAddress(addr);
    m_warnFrame->setVisible(seat.known && !seat.registered);
}

void WorkerDialog::updateVisibilityForEngineAndTarget()
{
    const MinerEngine eng = static_cast<MinerEngine>(m_comboEngine->currentData().toInt());
    const TargetType tgt = static_cast<TargetType>(m_comboTarget->currentData().toInt());

    m_cpuSection->setVisible(eng == MinerEngine::CpuMiner);
    m_gpuSection->setVisible(eng == MinerEngine::CcMiner);

    const bool isLocal = (tgt == TargetType::LocalWallet);
    const bool isPod = (tgt == TargetType::PodmanContainer);
    const bool isCustom = (tgt == TargetType::CustomRpc);
    const bool isSsh = (tgt == TargetType::RemoteSsh);

    auto setFormRowVisible = [this](QWidget* field, bool visible) {
        if (!field) return;
        field->setVisible(visible);
        if (m_form) {
            if (QWidget* lbl = m_form->labelForField(field)) {
                lbl->setVisible(visible);
            }
        }
    };

    setFormRowVisible(m_rowConf, isLocal);
    setFormRowVisible(m_rowPodman, isPod);
    setFormRowVisible(m_rowRpcUrl, isCustom || isSsh);
    setFormRowVisible(m_rowRpcAuth, isCustom || isSsh);
    setFormRowVisible(m_rowSsh, isSsh);
    setFormRowVisible(m_rowBinary, !isSsh);

    adjustSize();
}

void WorkerDialog::onBrowseBinary()
{
    const QString path = QFileDialog::getOpenFileName(this, KTr(QStringLiteral("dlg.binary")), m_editBinary->text());
    if (!path.isEmpty()) {
        m_editBinary->setText(path);
    }
}

void WorkerDialog::onBrowseConf()
{
    const QString path = QFileDialog::getOpenFileName(this, KTr(QStringLiteral("dlg.conf_path")), m_editConfPath->text());
    if (!path.isEmpty()) {
        m_editConfPath->setText(path);
    }
}

void WorkerDialog::onTestBinary()
{
    const QString exe = m_editBinary->text().trimmed();
    QProcess p;
    p.start(exe, {QStringLiteral("--version")});
    if (!p.waitForFinished(4000)) {
        QMessageBox::warning(this, KTr(QStringLiteral("dlg.test_bin")), QStringLiteral("Timed out running: ") + exe);
        return;
    }
    const QString out = QString::fromUtf8(p.readAllStandardOutput() + p.readAllStandardError()).trimmed();
    QMessageBox::information(this, KTr(QStringLiteral("dlg.test_bin")),
                             out.isEmpty() ? QStringLiteral("Exit code %1").arg(p.exitCode()) : out.left(800));
}

void WorkerDialog::onAcceptClicked()
{
    if (m_editName->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("Worker Name cannot be empty."));
        return;
    }
    const TargetType tgt = static_cast<TargetType>(m_comboTarget->currentData().toInt());
    if (tgt == TargetType::RemoteSsh && !isValidSshTarget(m_editSshTarget->text().trimmed())) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("Invalid SSH User@Host target."));
        return;
    }
    if (tgt == TargetType::CustomRpc || tgt == TargetType::RemoteSsh) {
        if (normalizeRpcUrl(m_editRpcUrl->text()).isEmpty()) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("Invalid RPC URL (expected http:// or https:// host[:port])."));
            return;
        }
        if (m_editRpcUser->text().trimmed().isEmpty() || m_editRpcPass->text().isEmpty()) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("RPC Username and RPC Password are required."));
            return;
        }
    }
    accept();
}

WorkerProfile WorkerDialog::resultProfile() const
{
    WorkerProfile p = m_original;
    p.name = m_editName->text().trimmed();
    const QString rawId = m_editId->text().trimmed();
    if (!m_isEdit) {
        p.id = sanitizeWorkerId(rawId.isEmpty() ? p.name : rawId);
    }
    p.engine = static_cast<MinerEngine>(m_comboEngine->currentData().toInt());
    p.binaryPath = m_editBinary->text().trimmed();
    p.targetType = static_cast<TargetType>(m_comboTarget->currentData().toInt());

    p.confPath = m_editConfPath->text().trimmed();
    p.podmanName = m_comboPodman->currentText().trimmed();
    if (p.targetType == TargetType::CustomRpc || p.targetType == TargetType::RemoteSsh) {
        const QString normUrl = normalizeRpcUrl(m_editRpcUrl->text());
        p.rpcUrl = normUrl.isEmpty() ? m_editRpcUrl->text().trimmed() : normUrl;
        p.rpcUser = m_editRpcUser->text().trimmed();
        p.rpcPassword = m_editRpcPass->text();
    } else {
        p.rpcUrl = m_editRpcUrl->text().trimmed();
        p.rpcUser.clear();
        p.rpcPassword.clear();
    }
    p.sshTarget = m_editSshTarget->text().trimmed();
    p.sshPort = m_spinSshPort->value();
    p.remoteBinaryPath = m_editRemoteBin->text().trimmed();

    p.coinbaseAddress = extractedCoinbaseAddress();
    p.threads = m_spinThreads->value();
    p.cpuAffinity = m_editAffinity->text().trimmed();
    p.maxTemp = m_spinMaxTemp->value();

    const int gIdx = m_comboGpuDevices->currentIndex();
    const QString gText = m_comboGpuDevices->currentText().trimmed();
    if (gIdx >= 0 && m_comboGpuDevices->itemText(gIdx) == gText && !m_comboGpuDevices->itemData(gIdx).toString().isEmpty()) {
        p.gpuDevices = m_comboGpuDevices->itemData(gIdx).toString().trimmed();
    } else {
        p.gpuDevices = gText;
    }
    p.gpuIntensity = m_spinGpuIntensity->value();

    p.extraArgs = m_editExtraArgs->text().trimmed();
    p.autoRestart = m_chkAutoRestart->isChecked();
    p.disableInternalGenOnStart = m_chkDisableInternalGen->isChecked();
    return p;
}
