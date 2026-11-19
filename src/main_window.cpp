#include "main_window.h"

#include "config_store.h"
#include "i18n.h"
#include "process_scanner.h"
#include "rpc_client.h"
#include "theme.h"
#include "worker_controller.h"
#include "worker_dialog.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFontMetrics>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QShowEvent>
#include <QSplitter>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>

#include <memory>

namespace {

class NumericSortItem : public QTableWidgetItem
{
public:
    using QTableWidgetItem::QTableWidgetItem;
    bool operator<(const QTableWidgetItem& other) const override
    {
        const QVariant a = data(Qt::UserRole + 10);
        const QVariant b = other.data(Qt::UserRole + 10);
        if (a.isValid() && b.isValid()) {
            return a.toDouble() < b.toDouble();
        }
        return QTableWidgetItem::operator<(other);
    }
};

} // namespace

MainWindow::MainWindow(ConfigStore* store,
                       RpcClient* rpc,
                       WorkerController* controller,
                       bool initialLightTheme,
                       QWidget* parent)
    : QMainWindow(parent)
    , m_store(store)
    , m_rpc(rpc)
    , m_controller(controller)
    , m_scanner(new ProcessScanner(this))
    , m_isLight(initialLightTheme)
{
    setupUi();
    setupTray();
    retranslateUi();

    connect(&I18n::instance(), &I18n::languageChanged, this, [this]() {
        retranslateUi();
    });
    connect(m_controller, &WorkerController::workersChanged, this, &MainWindow::refreshTableAndKpis);
    connect(m_controller, &WorkerController::workerLogAppended, this, &MainWindow::onWorkerLogAppended);
    connect(m_rpc, &RpcClient::targetStatusUpdated, this, [this]() { refreshTableAndKpis(); });
    connect(m_rpc, &RpcClient::poolSeatsUpdated, this, [this]() { refreshTableAndKpis(); });

    new QShortcut(QKeySequence::Quit, this, SLOT(requestQuit()));

    m_rpcTimer.setInterval(8000);
    connect(&m_rpcTimer, &QTimer::timeout, this, [this]() {
        m_rpc->refreshTargets(m_controller->profiles());
    });
    m_rpcTimer.start();

    m_hwTimer.setInterval(4000);
    connect(&m_hwTimer, &QTimer::timeout, this, &MainWindow::pollGpusAndPods);
    m_hwTimer.start();

    pollGpusAndPods();
    m_rpc->refreshTargets(m_controller->profiles());
    refreshTableAndKpis();
}

void MainWindow::setupUi()
{
    resize(1380, 860);
    setWindowIcon(Theme::appIcon());

    QWidget* central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout* root = new QVBoxLayout(central);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    // 1. Header Card
    QFrame* headerCard = new QFrame(central);
    headerCard->setObjectName(QStringLiteral("headerCard"));
    QHBoxLayout* hdrLay = new QHBoxLayout(headerCard);
    hdrLay->setContentsMargins(14, 10, 14, 10);
    hdrLay->setSpacing(10);

    QLabel* logoLbl = new QLabel(headerCard);
    const QPixmap logoPm = Theme::appIcon().pixmap(QSize(38, 38), qMax(qreal(1.0), devicePixelRatioF()));
    if (!logoPm.isNull()) {
        logoLbl->setPixmap(logoPm);
    }
    logoLbl->setFixedSize(38, 38);
    hdrLay->addWidget(logoLbl);

    QVBoxLayout* titleBox = new QVBoxLayout();
    titleBox->setSpacing(2);
    m_lblTitle = new QLabel(headerCard);
    m_lblTitle->setObjectName(QStringLiteral("appTitle"));
    m_lblSubtitle = new QLabel(headerCard);
    m_lblSubtitle->setObjectName(QStringLiteral("appSubtitle"));
    titleBox->addWidget(m_lblTitle);
    titleBox->addWidget(m_lblSubtitle);
    hdrLay->addLayout(titleBox, 1);

    auto makeBtn = [&](const QString& variant) {
        QPushButton* b = new QPushButton(headerCard);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        if (!variant.isEmpty()) b->setProperty("variant", variant);
        return b;
    };

    m_btnAdd = makeBtn(QStringLiteral("primary"));
    m_btnDiscover = makeBtn(QString());
    m_btnStartAll = makeBtn(QStringLiteral("primary"));
    m_btnStopAll = makeBtn(QStringLiteral("danger"));
    m_btnStopUnreg = makeBtn(QStringLiteral("salmon"));
    m_btnDisableGen = makeBtn(QString());
    m_btnImport = makeBtn(QString());
    m_btnExport = makeBtn(QString());
    m_btnLang = makeBtn(QString());
    m_btnTheme = makeBtn(QString());

    for (QPushButton* b : {m_btnAdd, m_btnDiscover, m_btnStartAll, m_btnStopAll,
                           m_btnStopUnreg, m_btnDisableGen, m_btnImport, m_btnExport, m_btnLang, m_btnTheme}) {
        hdrLay->addWidget(b);
    }
    root->addWidget(headerCard);

    // 2. KPI Strip (6 cards)
    QHBoxLayout* kpiRow = new QHBoxLayout();
    kpiRow->setSpacing(10);
    auto makeKpiCard = [&](QLabel*& titleOut, QLabel*& valOut, QLabel*& subOut) {
        QFrame* card = new QFrame(central);
        card->setObjectName(QStringLiteral("kpiCard"));
        QVBoxLayout* l = new QVBoxLayout(card);
        l->setContentsMargins(12, 8, 12, 8);
        l->setSpacing(2);
        titleOut = new QLabel(card);
        titleOut->setObjectName(QStringLiteral("kpiTitle"));
        valOut = new QLabel(QStringLiteral("—"), card);
        valOut->setObjectName(QStringLiteral("kpiValue"));
        subOut = new QLabel(QStringLiteral("—"), card);
        subOut->setObjectName(QStringLiteral("kpiSub"));
        l->addWidget(titleOut);
        l->addWidget(valOut);
        l->addWidget(subOut);
        kpiRow->addWidget(card, 1);
    };
    makeKpiCard(m_kpiTitleWorkers, m_kpiValWorkers, m_kpiSubWorkers);
    makeKpiCard(m_kpiTitlePool, m_kpiValPool, m_kpiSubPool);
    makeKpiCard(m_kpiTitleHash, m_kpiValHash, m_kpiSubHash);
    makeKpiCard(m_kpiTitleSol, m_kpiValSol, m_kpiSubSol);
    makeKpiCard(m_kpiTitleGpu, m_kpiValGpu, m_kpiSubGpu);
    makeKpiCard(m_kpiTitleNet, m_kpiValNet, m_kpiSubNet);
    root->addLayout(kpiRow);

    // 3. Filter Toolbar
    QFrame* toolbar = new QFrame(central);
    toolbar->setObjectName(QStringLiteral("toolbarCard"));
    QHBoxLayout* tbLay = new QHBoxLayout(toolbar);
    tbLay->setContentsMargins(12, 8, 12, 8);
    tbLay->setSpacing(8);

    m_statusFilterGroup = new QButtonGroup(this);
    m_statusFilterGroup->setExclusive(true);

    auto makePill = [&](int filterIdx) {
        QPushButton* b = new QPushButton(toolbar);
        b->setProperty("variant", QStringLiteral("pill"));
        b->setCheckable(true);
        b->setChecked(filterIdx == 0);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        m_statusFilterGroup->addButton(b, filterIdx);
        tbLay->addWidget(b);
        return b;
    };
    m_pillAll = makePill(0);
    m_pillRunning = makePill(1);
    m_pillStopped = makePill(2);
    m_pillWarnings = makePill(3);

    connect(m_statusFilterGroup, &QButtonGroup::idClicked, this, [this](int filterIdx) {
        m_statusFilter = filterIdx;
        onFilterChanged();
    });

    tbLay->addSpacing(8);
    m_comboEngineFilter = new QComboBox(toolbar);
    m_comboEngineFilter->addItem(QString(), -1);
    m_comboEngineFilter->addItem(QStringLiteral("cpuminer"), static_cast<int>(MinerEngine::CpuMiner));
    m_comboEngineFilter->addItem(QStringLiteral("ccminer"), static_cast<int>(MinerEngine::CcMiner));
    tbLay->addWidget(m_comboEngineFilter);

    m_comboTargetFilter = new QComboBox(toolbar);
    m_comboTargetFilter->addItem(QString(), -1);
    m_comboTargetFilter->addItem(QStringLiteral("Local"), static_cast<int>(TargetType::LocalWallet));
    m_comboTargetFilter->addItem(QStringLiteral("Podman"), static_cast<int>(TargetType::PodmanContainer));
    m_comboTargetFilter->addItem(QStringLiteral("Custom RPC"), static_cast<int>(TargetType::CustomRpc));
    m_comboTargetFilter->addItem(QStringLiteral("SSH"), static_cast<int>(TargetType::RemoteSsh));
    tbLay->addWidget(m_comboTargetFilter);

    m_editSearch = new QLineEdit(toolbar);
    m_editSearch->setClearButtonEnabled(true);
    tbLay->addWidget(m_editSearch, 1);

    m_lblFilterCount = new QLabel(QStringLiteral("0 / 0"), toolbar);
    m_lblFilterCount->setObjectName(QStringLiteral("mutedLabel"));
    tbLay->addWidget(m_lblFilterCount);
    root->addWidget(toolbar);

    // 4. Split Table + Inspector
    QSplitter* splitter = new QSplitter(Qt::Vertical, central);

    m_table = new QTableWidget(splitter);
    m_table->setColumnCount(8);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setWordWrap(false);
    m_table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSortingEnabled(true);
    m_table->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    splitter->addWidget(m_table);

    // Bottom Inspector Card
    QFrame* inspCard = new QFrame(splitter);
    inspCard->setObjectName(QStringLiteral("inspectorCard"));
    QVBoxLayout* inspLay = new QVBoxLayout(inspCard);
    inspLay->setContentsMargins(12, 10, 12, 10);
    inspLay->setSpacing(8);

    QHBoxLayout* inspTop = new QHBoxLayout();
    m_lblInspectorHeader = new QLabel(inspCard);
    m_lblInspectorHeader->setObjectName(QStringLiteral("kpiValue"));
    m_lblInspectorMeta = new QLabel(inspCard);
    m_lblInspectorMeta->setObjectName(QStringLiteral("mutedLabel"));
    m_btnCopyAddr = new QPushButton(inspCard);
    m_btnCopyAddr->setFocusPolicy(Qt::NoFocus);
    m_btnClearLog = new QPushButton(inspCard);
    m_btnClearLog->setFocusPolicy(Qt::NoFocus);
    m_chkAutoScroll = new QCheckBox(inspCard);
    m_chkAutoScroll->setChecked(true);
    inspTop->addWidget(m_lblInspectorHeader);
    inspTop->addSpacing(10);
    inspTop->addWidget(m_lblInspectorMeta, 1);
    inspTop->addWidget(m_chkAutoScroll);
    inspTop->addWidget(m_btnCopyAddr);
    inspTop->addWidget(m_btnClearLog);
    inspLay->addLayout(inspTop);

    m_warnBanner = new QFrame(inspCard);
    m_warnBanner->setObjectName(QStringLiteral("warningBanner"));
    QHBoxLayout* wbLay = new QHBoxLayout(m_warnBanner);
    wbLay->setContentsMargins(10, 6, 10, 6);
    m_lblWarnBanner = new QLabel(m_warnBanner);
    m_lblWarnBanner->setObjectName(QStringLiteral("warnLabel"));
    m_lblWarnBanner->setWordWrap(true);
    m_btnFixDoubleMine = new QPushButton(m_warnBanner);
    m_btnFixDoubleMine->setProperty("variant", QStringLiteral("salmon"));
    m_btnFixDoubleMine->setFocusPolicy(Qt::NoFocus);
    wbLay->addWidget(m_lblWarnBanner, 1);
    wbLay->addWidget(m_btnFixDoubleMine);
    m_warnBanner->setVisible(false);
    inspLay->addWidget(m_warnBanner);

    m_logViewer = new QPlainTextEdit(inspCard);
    m_logViewer->setObjectName(QStringLiteral("logViewer"));
    m_logViewer->setReadOnly(true);
    m_logViewer->setMaximumBlockCount(WorkerRuntime::kMaxLogLines);
    inspLay->addWidget(m_logViewer, 1);

    splitter->addWidget(inspCard);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    root->addWidget(splitter, 1);

    // Wire actions
    connect(m_btnAdd, &QPushButton::clicked, this, &MainWindow::onAddWorker);
    connect(m_btnDiscover, &QPushButton::clicked, this, &MainWindow::onDiscoverFleet);
    connect(m_btnStartAll, &QPushButton::clicked, this, [this]() { m_controller->startAll(); });
    connect(m_btnStopAll, &QPushButton::clicked, this, [this]() { m_controller->stopAll(false); });
    connect(m_btnStopUnreg, &QPushButton::clicked, this, &MainWindow::onStopUnregistered);
    connect(m_btnDisableGen, &QPushButton::clicked, this, &MainWindow::onToggleAllInternalGen);
    connect(m_btnImport, &QPushButton::clicked, this, &MainWindow::onImportProfiles);
    connect(m_btnExport, &QPushButton::clicked, this, &MainWindow::onExportProfiles);
    connect(m_btnLang, &QPushButton::clicked, this, []() { I18n::instance().toggleLanguage(); });
    connect(m_btnTheme, &QPushButton::clicked, this, &MainWindow::onToggleTheme);

    connect(m_comboEngineFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onFilterChanged);
    connect(m_comboTargetFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onFilterChanged);
    connect(m_editSearch, &QLineEdit::textChanged, this, &MainWindow::onFilterChanged);
    connect(m_table->horizontalHeader(), &QHeaderView::sortIndicatorChanged, this, [this]() {
        applyFilters();
        restoreTableSelection(m_selectedWorkerId, m_table->currentColumn());
    }, Qt::QueuedConnection);
    connect(m_table->horizontalHeader(), &QHeaderView::sectionResized, this,
            [this](int logicalIndex, int oldSize, int newSize) {
                if (!m_autoResizingColumns && m_initialColumnFitDone && m_table->isVisible() && oldSize != newSize) {
                    m_userResizedColumns.insert(logicalIndex);
                }
            });
    connect(m_table->horizontalHeader(), &QHeaderView::sectionHandleDoubleClicked, this,
            [this](int logicalIndex) {
                m_userResizedColumns.remove(logicalIndex);
                fitTableColumnToContents(logicalIndex, /*allowShrink=*/true);
            });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &MainWindow::onRowSelectionChanged);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (QTableWidgetItem* it = m_table->item(row, 0)) {
            onEditWorker(it->data(Qt::UserRole).toString());
        }
    });

    connect(m_btnCopyAddr, &QPushButton::clicked, this, [this]() {
        const WorkerProfile p = m_controller->profileById(m_selectedWorkerId);
        if (!p.coinbaseAddress.isEmpty()) {
            QApplication::clipboard()->setText(p.coinbaseAddress);
        }
    });
    connect(m_btnClearLog, &QPushButton::clicked, this, [this]() {
        if (!m_selectedWorkerId.isEmpty()) {
            m_controller->clearWorkerLog(m_selectedWorkerId);
            m_logViewer->clear();
        }
    });
    connect(m_btnFixDoubleMine, &QPushButton::clicked, this, [this]() {
        const WorkerProfile p = m_controller->profileById(m_selectedWorkerId);
        if (p.id.isEmpty()) return;
        m_rpc->setInternalGenerate(p, false, [this, p](bool ok, const QString& err) {
            if (ok) {
                QMessageBox::information(this, KTr(QStringLiteral("app.title")),
                                         KTr(QStringLiteral("msg.gen_disabled")).arg(p.targetKey()));
                m_rpc->refreshTargets(m_controller->profiles());
            } else {
                QMessageBox::warning(this, KTr(QStringLiteral("app.title")), err);
            }
        });
    });
}

void MainWindow::setupTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }
    m_tray = new QSystemTrayIcon(Theme::appIcon(), this);
    QMenu* menu = new QMenu(this);
    m_actTrayShow = menu->addAction(QString(), this, [this]() {
        showNormal();
        raise();
        activateWindow();
    });
    m_actTrayStartAll = menu->addAction(QString(), this, [this]() { m_controller->startAll(); });
    m_actTrayStopAll = menu->addAction(QString(), this, [this]() { m_controller->stopAll(false); });
    menu->addSeparator();
    m_actTrayQuit = menu->addAction(QString(), this, &MainWindow::requestQuit);

    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            if (isVisible()) hide();
            else {
                showNormal();
                raise();
                activateWindow();
            }
        }
    });
    m_tray->show();
}

void MainWindow::retranslateUi()
{
    setWindowTitle(KTr(QStringLiteral("app.title")));
    m_lblTitle->setText(KTr(QStringLiteral("app.title")));
    m_lblSubtitle->setText(KTr(QStringLiteral("app.subtitle")));

    m_btnAdd->setText(KTr(QStringLiteral("btn.add_worker")));
    m_btnDiscover->setText(KTr(QStringLiteral("btn.discover")));
    m_btnStartAll->setText(KTr(QStringLiteral("btn.start_all")));
    m_btnStopAll->setText(KTr(QStringLiteral("btn.stop_all")));
    m_btnStopUnreg->setText(KTr(QStringLiteral("btn.stop_unreg")));
    updateInternalGenButton();
    m_btnImport->setText(KTr(QStringLiteral("btn.import")));
    m_btnExport->setText(KTr(QStringLiteral("btn.export")));
    m_btnLang->setText(KTr(QStringLiteral("btn.lang")));
    m_btnTheme->setText(KTr(m_isLight ? QStringLiteral("btn.theme_light") : QStringLiteral("btn.theme_dark")));

    m_kpiTitleWorkers->setText(KTr(QStringLiteral("kpi.workers")));
    m_kpiTitlePool->setText(KTr(QStringLiteral("kpi.pool_seats")));
    m_kpiTitleHash->setText(KTr(QStringLiteral("kpi.hashrate")));
    m_kpiTitleSol->setText(KTr(QStringLiteral("kpi.solutions")));
    m_kpiTitleGpu->setText(KTr(QStringLiteral("kpi.gpu")));
    m_kpiTitleNet->setText(KTr(QStringLiteral("kpi.network")));

    m_pillAll->setText(KTr(QStringLiteral("filter.all")));
    m_pillRunning->setText(KTr(QStringLiteral("filter.running")));
    m_pillStopped->setText(KTr(QStringLiteral("filter.stopped")));
    m_pillWarnings->setText(KTr(QStringLiteral("filter.warnings")));

    m_comboEngineFilter->setItemText(0, KTr(QStringLiteral("filter.engine_all")));
    m_comboTargetFilter->setItemText(0, KTr(QStringLiteral("filter.target_all")));
    m_comboTargetFilter->setItemText(1, KTr(QStringLiteral("target.local")));
    m_comboTargetFilter->setItemText(2, KTr(QStringLiteral("target.podman")));
    m_comboTargetFilter->setItemText(3, KTr(QStringLiteral("target.custom")));
    m_comboTargetFilter->setItemText(4, KTr(QStringLiteral("target.ssh")));
    m_editSearch->setPlaceholderText(KTr(QStringLiteral("filter.search_ph")));

    {
        QScopedValueRollback<bool> guard(m_autoResizingColumns, true);
        m_table->setHorizontalHeaderLabels({
            KTr(QStringLiteral("col.worker")),
            KTr(QStringLiteral("col.engine")),
            KTr(QStringLiteral("col.target")),
            KTr(QStringLiteral("col.address")),
            KTr(QStringLiteral("col.pool")),
            KTr(QStringLiteral("col.state")),
            KTr(QStringLiteral("col.perf")),
            KTr(QStringLiteral("col.actions"))
        });
    }

    m_lblInspectorHeader->setText(KTr(QStringLiteral("log.header")));
    m_btnCopyAddr->setText(KTr(QStringLiteral("log.copy_addr")));
    m_btnClearLog->setText(KTr(QStringLiteral("log.clear")));
    m_chkAutoScroll->setText(KTr(QStringLiteral("log.autoscroll")));
    m_btnFixDoubleMine->setText(KTr(QStringLiteral("btn.fix_disable_gen")));

    if (m_tray) {
        m_tray->setToolTip(KTr(QStringLiteral("app.title")));
        m_actTrayShow->setText(KTr(QStringLiteral("tray.show")));
        m_actTrayStartAll->setText(KTr(QStringLiteral("btn.start_all")));
        m_actTrayStopAll->setText(KTr(QStringLiteral("btn.stop_all")));
        m_actTrayQuit->setText(KTr(QStringLiteral("tray.quit")));
    }

    refreshTableAndKpis();
    fitTableColumnsToContents(/*allowShrink=*/true);
}

void MainWindow::setLightTheme(bool isLight)
{
    m_isLight = isLight;
    QSettings settings(QStringLiteral("KristaTech"), QStringLiteral("MinerManager"));
    settings.setValue(QStringLiteral("ui/lightTheme"), m_isLight);
    {
        QScopedValueRollback<bool> guard(m_autoResizingColumns, true);
        qApp->setStyleSheet(Theme::buildStylesheet(m_isLight));
    }
    m_btnTheme->setText(KTr(m_isLight ? QStringLiteral("btn.theme_light") : QStringLiteral("btn.theme_dark")));
    refreshTableAndKpis();
    fitTableColumnsToContents(/*allowShrink=*/true);
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    if (!m_initialColumnFitDone) {
        m_initialColumnFitDone = true;
        fitTableColumnsToContents(/*allowShrink=*/true);
        ensureInitialWindowFitsTable();
    }
}

void MainWindow::ensureInitialWindowFitsTable()
{
    if (!m_table) return;
    int totalColsWidth = 0;
    for (int c = 0; c < m_table->columnCount(); ++c) {
        totalColsWidth += m_table->columnWidth(c);
    }
    const int chromeW = (m_table->verticalScrollBar() ? m_table->verticalScrollBar()->sizeHint().width() : 16)
                      + m_table->frameWidth() * 2 + 36;
    const int neededW = totalColsWidth + chromeW;
    if (neededW <= width()) return;

    QScreen* scr = screen();
    if (!scr) scr = QGuiApplication::primaryScreen();
    if (!scr) return;
    const int maxAllowedW = scr->availableGeometry().width() - 48;
    if (maxAllowedW > width()) {
        resize(qMin(neededW, maxAllowedW), height());
    }
}

int MainWindow::computeColumnFitWidth(int col) const
{
    if (!m_table || col < 0 || col >= m_table->columnCount()) {
        return 0;
    }

    m_table->ensurePolished();
    QHeaderView* hdr = m_table->horizontalHeader();
    hdr->ensurePolished();

    const int markReserve = m_table->isSortingEnabled()
        ? qMax(14, m_table->style()->pixelMetric(QStyle::PM_HeaderMarkSize, nullptr, hdr)
                   + m_table->style()->pixelMetric(QStyle::PM_HeaderMargin, nullptr, hdr))
        : 0;

    int width = hdr->sectionSizeHint(col);
    if (m_table->isSortingEnabled() && hdr->sortIndicatorSection() != col) {
        width += markReserve;
    }

    if (const QTableWidgetItem* hItem = m_table->horizontalHeaderItem(col)) {
        const QString hText = hItem->text();
        if (!hText.isEmpty()) {
            QFont hf = hdr->font();
            hf.setBold(true);
            const QFontMetrics hfm(hf);
            const int hTextW = qMax(hfm.horizontalAdvance(hText), hfm.boundingRect(hText).width());
            width = qMax(width, hTextW + 19 + markReserve);
        }
    }

    const int focusMargin = qMax(2, m_table->style()->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, m_table));
    const int cellHorizExtra = 16 + 2 * (focusMargin + 1) + 4;
    const QFontMetrics defaultFm(m_table->font());

    for (int r = 0; r < m_table->rowCount(); ++r) {
        if (QWidget* cw = m_table->cellWidget(r, col)) {
            cw->ensurePolished();
            const auto children = cw->findChildren<QWidget*>();
            for (QWidget* child : children) {
                child->ensurePolished();
                child->updateGeometry();
            }
            if (QLayout* lay = cw->layout()) {
                lay->invalidate();
                lay->activate();
            }
            width = qMax(width, qMax(cw->sizeHint().width(), cw->minimumSizeHint().width()) + 22);
        }

        if (const QTableWidgetItem* item = m_table->item(r, col)) {
            const QString text = item->text();
            if (!text.isEmpty()) {
                const QFontMetrics fm = item->data(Qt::FontRole).isValid()
                    ? QFontMetrics(item->font())
                    : defaultFm;
                const int textW = qMax(fm.horizontalAdvance(text), fm.boundingRect(text).width());
                width = qMax(width, textW + cellHorizExtra);
            }
        }
    }

    return qMax(width, hdr->minimumSectionSize());
}

void MainWindow::fitTableColumnToContents(int col, bool allowShrink)
{
    if (!m_table || col < 0 || col >= m_table->columnCount() || m_userResizedColumns.contains(col)) {
        return;
    }
    const int targetWidth = computeColumnFitWidth(col);
    const int currentWidth = m_table->columnWidth(col);
    if (allowShrink ? (targetWidth != currentWidth) : (targetWidth > currentWidth)) {
        QScopedValueRollback<bool> guard(m_autoResizingColumns, true);
        m_table->setColumnWidth(col, targetWidth);
    }
}

void MainWindow::fitTableColumnsToContents(bool allowShrink)
{
    if (!m_table) {
        return;
    }
    for (int c = 0; c < m_table->columnCount(); ++c) {
        fitTableColumnToContents(c, allowShrink);
    }
}

void MainWindow::onToggleTheme()
{
    setLightTheme(!m_isLight);
}

void MainWindow::pollGpusAndPods()
{
    m_pods = ProcessScanner::discoverPodmanContainers();
    m_scanner->queryNvidiaGpusAsync([this](const QVector<GpuTelemetry>& gpus) {
        m_gpus = gpus;
        updateKpis();
    });
}

void MainWindow::refreshTableAndKpis()
{
    rebuildTable();
    updateKpis();
    updateInspector();
}

void MainWindow::rebuildTable()
{
    if (!m_table) return;
    QScopedValueRollback<bool> rebuildGuard(m_rebuildingTable, true);
    QScopedValueRollback<bool> resizeGuard(m_autoResizingColumns, true);
    const ThemeColors tc = Theme::colors(m_isLight);
    const QVector<WorkerProfile>& profiles = m_controller->profiles();

    QString profileSignature;
    profileSignature.reserve(profiles.size() * 128);
    for (const WorkerProfile& p : profiles) {
        profileSignature.append(p.id).append(QLatin1Char('\x1f'))
            .append(p.name).append(QLatin1Char('\x1f'))
            .append(QString::number(static_cast<int>(p.engine))).append(QLatin1Char('\x1f'))
            .append(QString::number(p.threads)).append(QLatin1Char('\x1f'))
            .append(p.cpuAffinity).append(QLatin1Char('\x1f'))
            .append(p.gpuDevices).append(QLatin1Char('\x1f'))
            .append(QString::number(p.gpuIntensity, 'f', 1)).append(QLatin1Char('\x1f'))
            .append(QString::number(static_cast<int>(p.targetType))).append(QLatin1Char('\x1f'))
            .append(p.podmanName).append(QLatin1Char('\x1f'))
            .append(p.rpcUrl).append(QLatin1Char('\x1f'))
            .append(p.sshTarget).append(QLatin1Char('\x1f'))
            .append(p.coinbaseAddress).append(QLatin1Char('\x1e'));
    }
    const bool structureChanged = (profileSignature != m_lastProfileSignature);
    m_lastProfileSignature = profileSignature;

    const int savedVScroll = m_table->verticalScrollBar() ? m_table->verticalScrollBar()->value() : 0;
    const int savedHScroll = m_table->horizontalScrollBar() ? m_table->horizontalScrollBar()->value() : 0;
    const int prevCol = qBound(0, m_table->currentColumn(), qMax(0, m_table->columnCount() - 2));
    const QString prevSelected = m_selectedWorkerId;

    m_table->setUpdatesEnabled(false);
    m_table->clearSelection();
    if (m_table->selectionModel()) {
        m_table->selectionModel()->clearCurrentIndex();
    }
    m_table->setSortingEnabled(false);
    m_table->setRowCount(profiles.size());

    QFont btnFont = m_table->font();
    btnFont.setPixelSize(12);
    btnFont.setWeight(QFont::DemiBold);
    const QFontMetrics btnFm(btnFont);
    const QString startTxt = KTr(QStringLiteral("act.start"));
    const QString stopTxt = KTr(QStringLiteral("act.stop"));
    const QString editTxt = KTr(QStringLiteral("act.edit"));
    const int startW = qMax(btnFm.horizontalAdvance(startTxt), btnFm.boundingRect(startTxt).width());
    const int stopW = qMax(btnFm.horizontalAdvance(stopTxt), btnFm.boundingRect(stopTxt).width());
    const int editW = qMax(btnFm.horizontalAdvance(editTxt), btnFm.boundingRect(editTxt).width());
    const int toggleBtnW = qMax(startW, stopW) + 28; // 20px QSS padding + 2px border + 6px cushion
    const int editBtnW = editW + 28;

    for (int r = 0; r < profiles.size(); ++r) {
        const WorkerProfile& p = profiles[r];
        const WorkerRuntime rt = m_controller->runtimeById(p.id);
        const PoolSeatInfo seat = m_rpc->seatInfoForAddress(p.coinbaseAddress);
        const NodeMiningStatus nodeSt = m_rpc->statusForTarget(p.targetKey());

        const bool isRunning = (rt.pid > 0 && rt.status != WorkerStatus::Stopped && rt.status != WorkerStatus::Error) ||
                               rt.status == WorkerStatus::Restarting || rt.status == WorkerStatus::Starting;
        const bool unregWarn = !p.coinbaseAddress.isEmpty() && seat.known && !seat.registered;
        const bool rpcOfflineWarn = nodeSt.checked && !nodeSt.reachable;
        const bool missingAddrWarn = p.coinbaseAddress.isEmpty();
        const bool statusErrWarn = (rt.status == WorkerStatus::Error);
        const bool hasWarn = unregWarn || rt.warningDeferred || rt.warningRpcError ||
                             rpcOfflineWarn || missingAddrWarn || statusErrWarn;

        // Col 0: Worker Name + Search blob + filter user data
        QTableWidgetItem* itName = new NumericSortItem(p.name);
        itName->setData(Qt::UserRole, p.id);
        itName->setData(Qt::UserRole + 1, isRunning);
        itName->setData(Qt::UserRole + 2, hasWarn);
        itName->setData(Qt::UserRole + 3, static_cast<int>(p.engine));
        itName->setData(Qt::UserRole + 4, static_cast<int>(p.targetType));
        const QString searchBlob = QStringLiteral("%1 %2 %3 %4 %5 %6")
            .arg(p.name, p.id, p.coinbaseAddress, p.podmanName, p.sshTarget, p.rpcUrl);
        itName->setData(Qt::UserRole + 5, searchBlob);
        m_table->setItem(r, 0, itName);

        // Col 1: Engine & HW
        QString hwStr;
        if (p.engine == MinerEngine::CpuMiner) {
            hwStr = QStringLiteral("cpuminer • %1T").arg(p.threads);
            if (!p.cpuAffinity.isEmpty()) hwStr += QStringLiteral(" [c%1]").arg(p.cpuAffinity);
        } else {
            hwStr = QStringLiteral("ccminer • GPU %1").arg(p.gpuDevices.isEmpty() ? QStringLiteral("auto") : p.gpuDevices);
            if (p.gpuIntensity > 0) hwStr += QStringLiteral(" (i%1)").arg(p.gpuIntensity, 0, 'f', 1);
        }
        m_table->setItem(r, 1, new NumericSortItem(hwStr));

        // Col 2: Target
        QString tgtStr;
        switch (p.targetType) {
        case TargetType::LocalWallet:
            tgtStr = KTr(QStringLiteral("target.local"));
            break;
        case TargetType::PodmanContainer:
            tgtStr = p.podmanName.isEmpty() ? KTr(QStringLiteral("target.podman")) : p.podmanName;
            break;
        case TargetType::CustomRpc:
            tgtStr = p.rpcUrl;
            break;
        case TargetType::RemoteSsh:
            tgtStr = QStringLiteral("SSH: ") + p.sshTarget;
            break;
        }
        m_table->setItem(r, 2, new NumericSortItem(tgtStr));

        // Col 3: Coinbase Address
        QTableWidgetItem* itAddr = new NumericSortItem(
            p.coinbaseAddress.isEmpty() ? QStringLiteral("—") : p.coinbaseAddress);
        itAddr->setToolTip(p.coinbaseAddress);
        m_table->setItem(r, 3, itAddr);

        // Col 4: ADAM Pool Status
        QString poolText;
        QColor poolColor(tc.textSecondary);
        if (p.coinbaseAddress.isEmpty()) {
            poolText = QStringLiteral("—");
        } else if (rt.warningDeferred) {
            poolText = QStringLiteral("⚠ ") + KTr(QStringLiteral("pool.deferred"));
            poolColor = QColor(tc.warnFg);
        } else if (!seat.known && rpcOfflineWarn) {
            poolText = QStringLiteral("⚠ ") + KTr(QStringLiteral("pool.rpc_offline"));
            poolColor = QColor(tc.dangerFg);
        } else if (!seat.known) {
            poolText = KTr(QStringLiteral("pool.checking"));
        } else if (seat.registered) {
            poolText = QStringLiteral("✓ %1 (%2b)")
                .arg(seat.track)
                .arg(seat.remainingBlocks);
            poolColor = QColor(tc.successFg);
        } else {
            poolText = QStringLiteral("⚠ ") + KTr(QStringLiteral("pool.unregistered"));
            poolColor = QColor(tc.dangerFg);
        }
        QTableWidgetItem* itPool = new NumericSortItem(poolText);
        itPool->setForeground(poolColor);
        itPool->setData(Qt::UserRole + 10, seat.registered ? 2 : (seat.known ? 0 : 1));
        m_table->setItem(r, 4, itPool);

        // Col 5: Runtime State
        QString stText = KTr(statusToKey(rt.status));
        if (rt.pid > 0) {
            stText += QStringLiteral(" (PID %1)").arg(rt.pid);
        }
        if (rt.cpuTempC > 0) {
            stText += QStringLiteral(" • %1°C").arg(rt.cpuTempC);
        }
        QTableWidgetItem* itState = new NumericSortItem(stText);
        if (rt.status == WorkerStatus::Solving || rt.status == WorkerStatus::Adopted || rt.status == WorkerStatus::Waiting) {
            itState->setForeground(QColor(tc.successFg));
        } else if (rt.status == WorkerStatus::Error || rt.warningRpcError) {
            itState->setForeground(QColor(tc.dangerFg));
        } else if (rt.status == WorkerStatus::Restarting || rt.status == WorkerStatus::Starting) {
            itState->setForeground(QColor(tc.warnFg));
        } else {
            itState->setForeground(QColor(tc.textSecondary));
        }
        itState->setData(Qt::UserRole + 10, isRunning ? 2 : (rt.status == WorkerStatus::Error ? 1 : 0));
        m_table->setItem(r, 5, itState);

        // Col 6: Hashrate / A·S·R
        const QString hrStr = rt.hashrateDisplay.isEmpty() ? QStringLiteral("—") : rt.hashrateDisplay;
        const QString perfStr = QStringLiteral("%1 | %2/%3/%4")
            .arg(hrStr).arg(rt.accepted).arg(rt.stale).arg(rt.rejected);
        QTableWidgetItem* itPerf = new NumericSortItem(perfStr);
        itPerf->setData(Qt::UserRole + 10, rt.hashrateKhs + rt.accepted * 1000000.0);
        m_table->setItem(r, 6, itPerf);

        // Col 7: Compact row actions
        if (QWidget* oldAct = m_table->cellWidget(r, 7)) {
            oldAct->hide();
            m_table->removeCellWidget(r, 7);
        }
        QWidget* actBox = new QWidget(m_table->viewport());
        actBox->setObjectName(QStringLiteral("rowActionCell"));
        actBox->setAutoFillBackground(false);
        QHBoxLayout* actLay = new QHBoxLayout(actBox);
        actLay->setContentsMargins(2, 0, 2, 0);
        actLay->setSpacing(4);

        const QString id = p.id;
        QPushButton* bToggle = new QPushButton(isRunning ? stopTxt : startTxt, actBox);
        bToggle->setProperty("variant", isRunning ? QStringLiteral("danger") : QStringLiteral("primary"));
        bToggle->setProperty("compact", QStringLiteral("true"));
        bToggle->setFixedSize(toggleBtnW, 26);
        bToggle->setFocusPolicy(Qt::NoFocus);
        connect(bToggle, &QPushButton::clicked, this, [this, id, isRunning]() {
            if (isRunning) m_controller->stopWorker(id);
            else m_controller->startWorker(id);
        });

        QPushButton* bRestart = new QPushButton(QStringLiteral("↻"), actBox);
        bRestart->setToolTip(KTr(QStringLiteral("act.restart")));
        bRestart->setProperty("compact", QStringLiteral("icon"));
        bRestart->setFixedSize(28, 26);
        bRestart->setFocusPolicy(Qt::NoFocus);
        connect(bRestart, &QPushButton::clicked, this, [this, id]() { m_controller->restartWorker(id); });

        QPushButton* bEdit = new QPushButton(editTxt, actBox);
        bEdit->setProperty("compact", QStringLiteral("true"));
        bEdit->setFixedSize(editBtnW, 26);
        bEdit->setFocusPolicy(Qt::NoFocus);
        connect(bEdit, &QPushButton::clicked, this, [this, id]() { onEditWorker(id); });

        QPushButton* bDel = new QPushButton(QStringLiteral("✕"), actBox);
        bDel->setToolTip(KTr(QStringLiteral("act.delete")));
        bDel->setProperty("compact", QStringLiteral("icon"));
        bDel->setProperty("role", QStringLiteral("deleteIcon"));
        bDel->setFixedSize(28, 26);
        bDel->setFocusPolicy(Qt::NoFocus);
        connect(bDel, &QPushButton::clicked, this, [this, id]() { onDeleteWorker(id); });

        actLay->addWidget(bToggle);
        actLay->addWidget(bRestart);
        actLay->addWidget(bEdit);
        actLay->addWidget(bDel);
        m_table->setCellWidget(r, 7, actBox);
        m_table->setRowHeight(r, 36);
    }

    m_table->setSortingEnabled(true);
    applyFilters();

    restoreTableSelection(prevSelected, prevCol);
    if (m_table->verticalScrollBar()) {
        m_table->verticalScrollBar()->setValue(savedVScroll);
    }
    if (m_table->horizontalScrollBar()) {
        m_table->horizontalScrollBar()->setValue(savedHScroll);
    }
    fitTableColumnsToContents(/*allowShrink=*/!m_initialColumnFitDone || structureChanged);
    m_table->setUpdatesEnabled(true);
}

void MainWindow::restoreTableSelection(const QString& targetWorkerId, int preferredCol)
{
    if (!m_table || !m_table->selectionModel()) return;
    QScopedValueRollback<bool> guard(m_rebuildingTable, true);

    const int col = qBound(0, preferredCol, qMax(0, m_table->columnCount() - 2));
    int targetRow = -1;
    if (!targetWorkerId.isEmpty()) {
        for (int r = 0; r < m_table->rowCount(); ++r) {
            if (QTableWidgetItem* it = m_table->item(r, 0)) {
                if (it->data(Qt::UserRole).toString() == targetWorkerId) {
                    targetRow = r;
                    break;
                }
            }
        }
    }

    if (targetRow >= 0) {
        if (!m_table->isRowHidden(targetRow)) {
            const QModelIndex idx = m_table->model()->index(targetRow, col);
            m_table->selectionModel()->setCurrentIndex(
                idx,
                QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        } else {
            m_table->clearSelection();
            m_table->selectionModel()->clearCurrentIndex();
        }
        return;
    }

    int firstVisible = -1;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        if (!m_table->isRowHidden(r)) {
            firstVisible = r;
            break;
        }
    }
    if (firstVisible >= 0) {
        const QModelIndex idx = m_table->model()->index(firstVisible, col);
        m_table->selectionModel()->setCurrentIndex(
            idx,
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        if (QTableWidgetItem* it = m_table->item(firstVisible, 0)) {
            m_selectedWorkerId = it->data(Qt::UserRole).toString();
        }
    } else {
        m_table->clearSelection();
        m_table->selectionModel()->clearCurrentIndex();
        m_selectedWorkerId.clear();
    }
}

void MainWindow::applyFilters()
{
    if (!m_table) return;
    const int engFilter = m_comboEngineFilter->currentData().toInt();
    const int tgtFilter = m_comboTargetFilter->currentData().toInt();
    const QString q = m_editSearch->text().trimmed();

    if (m_statusFilterGroup) {
        if (QAbstractButton* btn = m_statusFilterGroup->button(m_statusFilter)) {
            btn->setChecked(true);
        }
    }

    int visible = 0;
    int runningCount = 0;
    int stoppedCount = 0;
    int warnCount = 0;
    const int total = m_table->rowCount();
    for (int r = 0; r < total; ++r) {
        QTableWidgetItem* it = m_table->item(r, 0);
        if (!it) continue;
        const bool isRunning = it->data(Qt::UserRole + 1).toBool();
        const bool hasWarn = it->data(Qt::UserRole + 2).toBool();
        const int eng = it->data(Qt::UserRole + 3).toInt();
        const int tgt = it->data(Qt::UserRole + 4).toInt();
        const QString blob = it->data(Qt::UserRole + 5).toString();

        if (isRunning) ++runningCount;
        else ++stoppedCount;
        if (hasWarn) ++warnCount;

        bool hide = false;
        if (m_statusFilter == 1 && !isRunning) hide = true;
        else if (m_statusFilter == 2 && isRunning) hide = true;
        else if (m_statusFilter == 3 && !hasWarn) hide = true;

        if (!hide && engFilter >= 0 && eng != engFilter) hide = true;
        if (!hide && tgtFilter >= 0 && tgt != tgtFilter) hide = true;
        if (!hide && !q.isEmpty() && !blob.contains(q, Qt::CaseInsensitive)) hide = true;

        m_table->setRowHidden(r, hide);
        if (!hide) ++visible;
    }

    m_pillAll->setText(QStringLiteral("%1 (%2)").arg(KTr(QStringLiteral("filter.all"))).arg(total));
    m_pillRunning->setText(QStringLiteral("%1 (%2)").arg(KTr(QStringLiteral("filter.running"))).arg(runningCount));
    m_pillStopped->setText(QStringLiteral("%1 (%2)").arg(KTr(QStringLiteral("filter.stopped"))).arg(stoppedCount));
    m_pillWarnings->setText(QStringLiteral("%1 (%2)").arg(KTr(QStringLiteral("filter.warnings"))).arg(warnCount));
    m_lblFilterCount->setText(QStringLiteral("%1 / %2").arg(visible).arg(total));
}

void MainWindow::onFilterChanged()
{
    applyFilters();
    restoreTableSelection(m_selectedWorkerId, m_table ? m_table->currentColumn() : 0);
}

void MainWindow::updateKpis()
{
    const auto& profiles = m_controller->profiles();
    int total = profiles.size();
    int running = 0;
    int cpuCount = 0;
    int gpuCount = 0;
    int regCount = 0;
    int unregWasteCount = 0;
    double sumKhs = 0.0;
    int sumAcc = 0, sumStale = 0, sumRej = 0;

    qint64 bestHeight = 0;
    int poolSize = m_rpc->allKnownSeats().size();
    int doubleMineTargets = 0;
    QSet<QString> seenTargetKeys;

    for (const WorkerProfile& p : profiles) {
        if (p.engine == MinerEngine::CpuMiner) ++cpuCount;
        else ++gpuCount;

        const WorkerRuntime rt = m_controller->runtimeById(p.id);
        const bool active = (rt.pid > 0 && rt.status != WorkerStatus::Stopped && rt.status != WorkerStatus::Error);
        if (active) {
            ++running;
            sumKhs += rt.hashrateKhs;
        }
        sumAcc += rt.accepted;
        sumStale += rt.stale;
        sumRej += rt.rejected;

        const PoolSeatInfo seat = m_rpc->seatInfoForAddress(p.coinbaseAddress);
        if (seat.registered) ++regCount;
        else if (active && (rt.warningDeferred || (seat.known && !seat.registered))) ++unregWasteCount;

        const QString tkey = p.targetKey();
        if (!seenTargetKeys.contains(tkey)) {
            seenTargetKeys.insert(tkey);
            const NodeMiningStatus ns = m_rpc->statusForTarget(tkey);
            if (ns.reachable) {
                bestHeight = qMax(bestHeight, ns.blockHeight);
                if (ns.internalGenerate) ++doubleMineTargets;
            }
        }
    }

    m_kpiValWorkers->setText(QStringLiteral("%1 / %2").arg(running).arg(total));
    m_kpiSubWorkers->setText(QStringLiteral("CPU: %1 • GPU: %2").arg(cpuCount).arg(gpuCount));

    auto setKpiWarnState = [](QLabel* lbl, bool warn) {
        const QString target = warn ? QStringLiteral("warn") : QString();
        if (lbl->property("state").toString() != target) {
            lbl->setProperty("state", target);
            lbl->style()->unpolish(lbl);
            lbl->style()->polish(lbl);
        }
    };

    m_kpiValPool->setText(QStringLiteral("%1 / %2").arg(regCount).arg(total));
    m_kpiSubPool->setText(unregWasteCount > 0
        ? QStringLiteral("⚠ %1 running unreg").arg(unregWasteCount)
        : QStringLiteral("Pool total: %1").arg(poolSize));
    setKpiWarnState(m_kpiSubPool, unregWasteCount > 0);

    m_kpiValHash->setText(formatHashrateKhs(sumKhs));
    m_kpiSubHash->setText(QStringLiteral("%1 active workers").arg(running));

    m_kpiValSol->setText(QStringLiteral("%1 / %2 / %3").arg(sumAcc).arg(sumStale).arg(sumRej));
    m_kpiSubSol->setText(QStringLiteral("Session totals"));

    if (!m_gpus.isEmpty()) {
        const GpuTelemetry& g = m_gpus.first();
        m_kpiValGpu->setText(QStringLiteral("%1°C • %2W").arg(g.tempC).arg(g.powerW, 0, 'f', 0));
        m_kpiSubGpu->setText(QStringLiteral("%1 (%2% util)").arg(g.name).arg(g.utilPct));
    } else {
        m_kpiValGpu->setText(QStringLiteral("—"));
        m_kpiSubGpu->setText(KTr(QStringLiteral("kpi.no_gpu")));
    }

    m_kpiValNet->setText(bestHeight > 0 ? QStringLiteral("Block #%1").arg(bestHeight) : QStringLiteral("RPC Syncing..."));
    m_kpiSubNet->setText(doubleMineTargets > 0
        ? QStringLiteral("%1 (%2 nodes)").arg(KTr(QStringLiteral("kpi.double_mine_warn"))).arg(doubleMineTargets)
        : KTr(QStringLiteral("kpi.double_mine_clean")));
    setKpiWarnState(m_kpiSubNet, doubleMineTargets > 0);
    updateInternalGenButton();
}

void MainWindow::updateInternalGenButton()
{
    if (!m_btnDisableGen) return;
    QSet<QString> seen;
    int reachableTargets = 0;
    int enabledTargets = 0;
    for (const WorkerProfile& p : m_controller->profiles()) {
        if (p.targetType == TargetType::RemoteSsh) continue;
        const QString tkey = p.targetKey();
        if (seen.contains(tkey)) continue;
        seen.insert(tkey);
        const NodeMiningStatus ns = m_rpc->statusForTarget(tkey);
        if (!ns.reachable) continue;
        ++reachableTargets;
        if (ns.internalGenerate) ++enabledTargets;
    }
    if (enabledTargets > 0) {
        m_btnDisableGen->setText(KTr(QStringLiteral("btn.disable_gen")));
        m_btnDisableGen->setToolTip(QStringLiteral("setgenerate false (%1 node(s))").arg(enabledTargets));
    } else {
        m_btnDisableGen->setText(KTr(QStringLiteral("btn.enable_gen")));
        m_btnDisableGen->setToolTip(reachableTargets > 0
            ? QStringLiteral("setgenerate true (%1 node(s))").arg(reachableTargets)
            : KTr(QStringLiteral("pool.checking")));
    }
}

void MainWindow::onRowSelectionChanged()
{
    if (m_rebuildingTable) return;
    const auto items = m_table->selectedItems();
    if (items.isEmpty()) return;
    QTableWidgetItem* it = m_table->item(items.first()->row(), 0);
    if (!it) return;
    const QString id = it->data(Qt::UserRole).toString();
    if (!id.isEmpty() && id != m_selectedWorkerId) {
        m_selectedWorkerId = id;
        updateInspector();
    }
}

void MainWindow::updateInspector()
{
    if (m_selectedWorkerId.isEmpty()) {
        m_lblInspectorMeta->setText(KTr(QStringLiteral("log.select_hint")));
        m_warnBanner->setVisible(false);
        m_logViewer->clear();
        return;
    }

    const WorkerProfile p = m_controller->profileById(m_selectedWorkerId);
    if (p.id.isEmpty()) {
        m_warnBanner->setVisible(false);
        m_logViewer->clear();
        return;
    }
    const WorkerRuntime rt = m_controller->runtimeById(p.id);
    const PoolSeatInfo seat = m_rpc->seatInfoForAddress(p.coinbaseAddress);

    QString meta = QStringLiteral("%1  •  PID: %2").arg(p.name).arg(rt.pid > 0 ? QString::number(rt.pid) : QStringLiteral("—"));
    if (rt.adopted) meta += QStringLiteral(" (Adopted)");
    if (rt.lastBlock > 0) meta += QStringLiteral("  •  Block #%1").arg(rt.lastBlock);
    m_lblInspectorMeta->setText(meta);

    const bool wasteWarn = rt.warningDeferred || (!p.coinbaseAddress.isEmpty() && seat.known && !seat.registered);
    if (wasteWarn) {
        const QString msg = (rt.warningDeferred && !rt.warningDeferredMsg.isEmpty())
            ? (KTr(QStringLiteral("warn.waste_banner")) + QStringLiteral(" (") + rt.warningDeferredMsg + QLatin1Char(')'))
            : KTr(QStringLiteral("warn.waste_banner"));
        m_lblWarnBanner->setText(msg);
        m_btnFixDoubleMine->setVisible(false);
        m_warnBanner->setVisible(true);
    } else {
        m_warnBanner->setVisible(false);
    }

    m_logViewer->setPlainText(rt.logLines.join(QLatin1Char('\n')));
    if (m_chkAutoScroll->isChecked()) {
        m_logViewer->verticalScrollBar()->setValue(m_logViewer->verticalScrollBar()->maximum());
    }
}

void MainWindow::onWorkerLogAppended(const QString& id, const QStringList& newLines)
{
    if (id != m_selectedWorkerId || newLines.isEmpty()) {
        return;
    }
    for (const QString& l : newLines) {
        m_logViewer->appendPlainText(l);
    }
    if (m_chkAutoScroll->isChecked()) {
        m_logViewer->verticalScrollBar()->setValue(m_logViewer->verticalScrollBar()->maximum());
    }
}

void MainWindow::onAddWorker()
{
    m_pods = ProcessScanner::discoverPodmanContainers();
    WorkerDialog dlg(nullptr, m_rpc, m_pods, m_gpus, this);
    if (dlg.exec() == QDialog::Accepted) {
        const QString newId = m_controller->addProfile(dlg.resultProfile());
        m_selectedWorkerId = newId;
        restoreTableSelection(m_selectedWorkerId, 0);
        updateInspector();
        m_rpc->refreshTargets(m_controller->profiles());
    }
}

void MainWindow::onEditWorker(const QString& id)
{
    const WorkerProfile existing = m_controller->profileById(id);
    if (existing.id.isEmpty()) return;
    m_pods = ProcessScanner::discoverPodmanContainers();
    WorkerDialog dlg(&existing, m_rpc, m_pods, m_gpus, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_controller->updateProfile(dlg.resultProfile());
        m_rpc->refreshTargets(m_controller->profiles());
    }
}

void MainWindow::onDeleteWorker(const QString& id)
{
    const WorkerProfile p = m_controller->profileById(id);
    if (p.id.isEmpty()) return;
    if (QMessageBox::question(this,
                              KTr(QStringLiteral("confirm.delete_title")),
                              KTr(QStringLiteral("confirm.delete_msg")).arg(p.name)) != QMessageBox::Yes) {
        return;
    }
    m_controller->removeProfile(id, /*stopIfRunning=*/true);
}

void MainWindow::onDiscoverFleet()
{
    m_controller->autoDiscoverFleet([this](int added, int adopted) {
        m_rpc->refreshTargets(m_controller->profiles());
        QMessageBox::information(this,
                                 KTr(QStringLiteral("btn.discover")),
                                 KTr(QStringLiteral("msg.discover_done")).arg(added).arg(adopted));
    });
}

void MainWindow::onStopUnregistered()
{
    const int n = m_controller->countRunningUnregisteredOrDeferred();
    if (n <= 0) return;
    if (QMessageBox::question(this,
                              KTr(QStringLiteral("confirm.stop_unreg_title")),
                              KTr(QStringLiteral("confirm.stop_unreg_msg")).arg(n)) != QMessageBox::Yes) {
        return;
    }
    m_controller->stopUnregisteredOrDeferred();
}

void MainWindow::onToggleAllInternalGen()
{
    QSet<QString> seen;
    QVector<WorkerProfile> enabledProfiles;
    QVector<WorkerProfile> disabledProfiles;
    for (const WorkerProfile& p : m_controller->profiles()) {
        if (p.targetType == TargetType::RemoteSsh) continue;
        const QString tkey = p.targetKey();
        if (seen.contains(tkey)) continue;
        seen.insert(tkey);
        const NodeMiningStatus ns = m_rpc->statusForTarget(tkey);
        if (!ns.reachable) continue;
        if (ns.internalGenerate) {
            enabledProfiles.append(p);
        } else {
            disabledProfiles.append(p);
        }
    }

    const bool targetEnable = enabledProfiles.isEmpty();
    const QVector<WorkerProfile>& batch = targetEnable ? disabledProfiles : enabledProfiles;
    if (batch.isEmpty()) {
        return;
    }

    auto remaining = std::make_shared<int>(batch.size());
    auto firstError = std::make_shared<QString>();
    QPointer<MainWindow> self(this);
    for (const WorkerProfile& p : batch) {
        m_rpc->setInternalGenerate(p, targetEnable, [self, remaining, firstError](bool ok, const QString& err) {
            if (!ok && firstError->isEmpty() && !err.isEmpty()) {
                *firstError = err;
            }
            --(*remaining);
            if (*remaining <= 0 && self) {
                self->m_rpc->refreshTargets(self->m_controller->profiles());
                if (!firstError->isEmpty()) {
                    QMessageBox::warning(self, KTr(QStringLiteral("app.title")), *firstError);
                }
            }
        });
    }
}

void MainWindow::onImportProfiles()
{
    const QString path = QFileDialog::getOpenFileName(this, KTr(QStringLiteral("btn.import")),
                                                      QDir::homePath(), QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, KTr(QStringLiteral("btn.import")), f.errorString());
        return;
    }
    QString err;
    const QVector<WorkerProfile> imported = m_store->parseImportedProfilesJson(f.readAll(), &err);
    if (!err.isEmpty()) {
        QMessageBox::warning(this, KTr(QStringLiteral("btn.import")), err);
        return;
    }
    int added = 0;
    m_controller->importProfiles(imported, &added);
    QMessageBox::information(this, KTr(QStringLiteral("btn.import")),
                             KTr(QStringLiteral("msg.import_ok")).arg(added).arg(path));
}

void MainWindow::onExportProfiles()
{
    const QString path = QFileDialog::getSaveFileName(this, KTr(QStringLiteral("btn.export")),
                                                      QDir::homePath() + QStringLiteral("/kristatech_miners.json"),
                                                      QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) return;
    QString err;
    const QByteArray bytes = m_store->exportProfilesJson(m_controller->profiles(), /*redactSecrets=*/true);
    if (!ConfigStore::writeFile0600(path, bytes, &err)) {
        QMessageBox::warning(this, KTr(QStringLiteral("btn.export")), err);
        return;
    }
    QMessageBox::information(this, KTr(QStringLiteral("btn.export")),
                             KTr(QStringLiteral("msg.export_ok")).arg(path));
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_reallyQuitting) {
        event->accept();
        return;
    }
    if (m_tray && m_tray->isVisible()) {
        hide();
        if (!m_trayNotifiedOnce) {
            m_trayNotifiedOnce = true;
            m_tray->showMessage(KTr(QStringLiteral("tray.minimized_title")),
                                KTr(QStringLiteral("tray.minimized_msg")),
                                QSystemTrayIcon::Information, 3500);
        }
        event->ignore();
        return;
    }
    event->ignore();
    requestQuit();
}

void MainWindow::requestQuit()
{
    const int runningLocal = m_controller->countRunningLocalOrPodman();
    if (runningLocal > 0) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(KTr(QStringLiteral("quit.title")));
        box.setText(KTr(QStringLiteral("quit.msg")).arg(runningLocal));
        QPushButton* btnStop = box.addButton(KTr(QStringLiteral("quit.stop_and_exit")), QMessageBox::DestructiveRole);
        QPushButton* btnDetach = box.addButton(KTr(QStringLiteral("quit.detach_and_exit")), QMessageBox::AcceptRole);
        QPushButton* btnCancel = box.addButton(KTr(QStringLiteral("quit.cancel")), QMessageBox::RejectRole);
        box.setDefaultButton(btnDetach);
        box.exec();

        if (box.clickedButton() == btnCancel || box.clickedButton() == nullptr) {
            return;
        }
        if (box.clickedButton() == btnStop) {
            m_controller->stopAllManagedAndCleanup();
        } else {
            m_controller->detachAll();
        }
    } else {
        m_controller->detachAll();
    }

    m_reallyQuitting = true;
    qApp->quit();
}
