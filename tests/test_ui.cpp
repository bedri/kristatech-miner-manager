#include "config_store.h"
#include "i18n.h"
#include "main_window.h"
#include "models.h"
#include "rpc_client.h"
#include "theme.h"
#include "worker_controller.h"
#include "worker_dialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListView>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTreeView>
#include <QtTest>

namespace {

void verifyAllColumnsFit(QTableWidget* table)
{
    QVERIFY(table != nullptr);
    QCOMPARE(table->columnCount(), 8);
    QCOMPARE(table->wordWrap(), false);

    QHeaderView* hdr = table->horizontalHeader();
    QVERIFY(hdr != nullptr);
    QCOMPARE(hdr->stretchLastSection(), false);
    for (int col = 0; col < table->columnCount(); ++col) {
        QCOMPARE(hdr->sectionResizeMode(col), QHeaderView::Interactive);
    }

    QFont hf = hdr->font();
    hf.setBold(true);
    const QFontMetrics hfm(hf);
    for (int col = 0; col < table->columnCount(); ++col) {
        const QTableWidgetItem* hItem = table->horizontalHeaderItem(col);
        QVERIFY(hItem != nullptr);
        const QString hText = hItem->text();
        const int hTextW = qMax(hfm.horizontalAdvance(hText), hfm.boundingRect(hText).width());
        const QByteArray msg = QStringLiteral("Col %1 header '%2' width %3 < required %4")
            .arg(col).arg(hText).arg(table->columnWidth(col)).arg(hTextW + 18).toUtf8();
        QVERIFY2(table->columnWidth(col) >= hTextW + 18, msg.constData());
    }

    const QFontMetrics defaultFm(table->font());
    for (int r = 0; r < table->rowCount(); ++r) {
        for (int col = 0; col < 7; ++col) {
            const QTableWidgetItem* item = table->item(r, col);
            if (!item || item->text().isEmpty()) continue;
            const QFontMetrics fm = item->data(Qt::FontRole).isValid()
                ? QFontMetrics(item->font())
                : defaultFm;
            const int textW = qMax(fm.horizontalAdvance(item->text()), fm.boundingRect(item->text()).width());
            const QByteArray msg = QStringLiteral("Row %1 col %2 text '%3' width %4 < required %5")
                .arg(r).arg(col).arg(item->text()).arg(table->columnWidth(col)).arg(textW + 20).toUtf8();
            QVERIFY2(table->columnWidth(col) >= textW + 20, msg.constData());
        }

        QWidget* cw = table->cellWidget(r, 7);
        QVERIFY(cw != nullptr);
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
        const int widgetW = qMax(cw->sizeHint().width(), cw->minimumSizeHint().width());
        const QByteArray msg = QStringLiteral("Row %1 col 7 widget width %2 < required %3")
            .arg(r).arg(table->columnWidth(7)).arg(widgetW + 16).toUtf8();
        QVERIFY2(table->columnWidth(7) >= widgetW + 16, msg.constData());

        const auto btns = cw->findChildren<QPushButton*>();
        QCOMPARE(btns.size(), 4);
        for (QPushButton* btn : btns) {
            const QFontMetrics bfm(btn->font());
            const QString bText = btn->text();
            const int btnTextW = qMax(bfm.horizontalAdvance(bText), bfm.boundingRect(bText).width());
            const QString compactKind = btn->property("compact").toString();
            const QByteArray hMsg = QStringLiteral("Row %1 button '%2' height %3 < 24")
                .arg(r).arg(bText).arg(btn->height()).toUtf8();
            QVERIFY2(btn->height() >= 24, hMsg.constData());
            if (compactKind == QStringLiteral("true")) {
                const QByteArray wMsg = QStringLiteral("Row %1 button '%2' width %3 < required %4")
                    .arg(r).arg(bText).arg(btn->width()).arg(btnTextW + 22).toUtf8();
                QVERIFY2(btn->width() >= btnTextW + 22, wMsg.constData());
            } else {
                const QByteArray wMsg = QStringLiteral("Row %1 icon button '%2' width %3 < required %4")
                    .arg(r).arg(bText).arg(btn->width()).arg(qMax(26, btnTextW + 6)).toUtf8();
                QVERIFY2(btn->width() >= 26 && btn->width() >= btnTextW + 6, wMsg.constData());
            }
        }
    }
}

} // namespace

class TestUi : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void testAppIconIsSquareAndMultiSize();
    void testEmptyTableColumnsFitHeaders();
    void testPopulatedTableColumnsFitContentAndWidgets();
    void testManualResizePreservedAndDoubleClickResets();
    void testSelectionPreservedAcrossTableRefreshAndSorting();
    void testFilterPillsAndWarningsFilter();
    void testToggleInternalGenButtonBidirectional();
    void testAutoStopDeferredCheckboxAndI18n();
    void testActionButtonsNotClippedWithVerticalScrollbar();
    void testWorkerDialogCustomRpcAndRemoteSshVisibilityAndValidation();
    void testFileDialogAndItemViewsThemeContrast();
    void testFairnessWalletLockAndSeatExpiryUi();
    void testSecurityWarningsAndImportGuard();

private:
    QTemporaryDir m_settingsDir;
};

void TestUi::initTestCase()
{
    QVERIFY(m_settingsDir.isValid());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, m_settingsDir.path());
    qApp->setApplicationName(QStringLiteral("KristaTechMinerManagerTest"));
    qApp->setOrganizationName(QStringLiteral("KristaTechTest"));
    Theme::applyTheme(qApp, false);
}

void TestUi::testEmptyTableColumnsFitHeaders()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    ConfigStore store(tmpDir.path());
    RpcClient rpc;
    WorkerController controller(&store, &rpc);
    controller.loadInitialState(/*autoDiscoverIfEmpty=*/false);

    I18n::instance().setLanguage(I18n::Language::TR);
    MainWindow win(&store, &rpc, &controller, false);
    win.show();
    QApplication::processEvents();

    QTableWidget* table = win.findChild<QTableWidget*>();
    verifyAllColumnsFit(table);

    I18n::instance().setLanguage(I18n::Language::EN);
    QApplication::processEvents();
    verifyAllColumnsFit(table);
}

void TestUi::testPopulatedTableColumnsFitContentAndWidgets()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    ConfigStore store(tmpDir.path());
    RpcClient rpc;
    WorkerController controller(&store, &rpc);
    controller.loadInitialState(/*autoDiscoverIfEmpty=*/false);

    WorkerProfile cpu;
    cpu.id = QStringLiteral("cpu_primary");
    cpu.name = QStringLiteral("worker_cpu_primary_istanbul_node_01");
    cpu.engine = MinerEngine::CpuMiner;
    cpu.threads = 8;
    cpu.cpuAffinity = QStringLiteral("0-7");
    cpu.targetType = TargetType::LocalWallet;
    cpu.confPath = QStringLiteral("/home/user/.kristatech/kristatech.conf");
    cpu.coinbaseAddress = QStringLiteral("KTdHzRF4e71xcU1ea8tePLmZNijUnH6ZNYm");
    controller.addProfile(cpu);

    WorkerProfile gpu;
    gpu.id = QStringLiteral("gpu_0");
    gpu.name = QStringLiteral("worker_gpu_rtx3090_0");
    gpu.engine = MinerEngine::CcMiner;
    gpu.gpuDevices = QStringLiteral("0,1");
    gpu.gpuIntensity = 21.5;
    gpu.targetType = TargetType::CustomRpc;
    gpu.rpcUrl = QStringLiteral("http://127.0.0.1:19999/very-long-rpc-path/");
    gpu.coinbaseAddress = QStringLiteral("KT9XyZ8877665544332211aAbBcCdDeEfFg");
    controller.addProfile(gpu);

    WorkerProfile ssh;
    ssh.id = QStringLiteral("ssh_02");
    ssh.name = QStringLiteral("worker_ssh_remote_02");
    ssh.engine = MinerEngine::CpuMiner;
    ssh.threads = 16;
    ssh.targetType = TargetType::RemoteSsh;
    ssh.sshTarget = QStringLiteral("miner@gpu-rig-istanbul-01.local");
    ssh.rpcUrl = QStringLiteral("http://10.20.30.40:27998/");
    ssh.coinbaseAddress = QStringLiteral("KTabCdEfGhIjKlMnOpQrStUvWxYz1234567");
    controller.addProfile(ssh);

    I18n::instance().setLanguage(I18n::Language::TR);
    MainWindow win(&store, &rpc, &controller, false);
    win.show();
    QApplication::processEvents();

    QTableWidget* table = win.findChild<QTableWidget*>();
    verifyAllColumnsFit(table);

    I18n::instance().setLanguage(I18n::Language::EN);
    QApplication::processEvents();
    verifyAllColumnsFit(table);

    win.setLightTheme(true);
    QApplication::processEvents();
    verifyAllColumnsFit(table);

    win.setLightTheme(false);
    I18n::instance().setLanguage(I18n::Language::TR);
    QApplication::processEvents();
    verifyAllColumnsFit(table);

    const int origCol0 = table->columnWidth(0);

    WorkerProfile extra;
    extra.id = QStringLiteral("extra_long");
    extra.name = QStringLiteral("worker_extra_ultra_long_identifier_for_column_expansion_verification_999");
    extra.engine = MinerEngine::CpuMiner;
    extra.threads = 4;
    extra.targetType = TargetType::LocalWallet;
    extra.coinbaseAddress = QStringLiteral("KTdHzRF4e71xcU1ea8tePLmZNijUnH6ZNYm");
    const QString extraId = controller.addProfile(extra);
    QApplication::processEvents();

    verifyAllColumnsFit(table);
    QVERIFY(table->columnWidth(0) > origCol0);

    QVERIFY(controller.removeProfile(extraId, /*stopIfRunning=*/false));
    QApplication::processEvents();

    verifyAllColumnsFit(table);
    QCOMPARE(table->columnWidth(0), origCol0);
}

void TestUi::testManualResizePreservedAndDoubleClickResets()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    ConfigStore store(tmpDir.path());
    RpcClient rpc;
    WorkerController controller(&store, &rpc);
    controller.loadInitialState(/*autoDiscoverIfEmpty=*/false);

    WorkerProfile cpu;
    cpu.id = QStringLiteral("cpu_1");
    cpu.name = QStringLiteral("worker_cpu_1");
    cpu.engine = MinerEngine::CpuMiner;
    cpu.threads = 4;
    cpu.targetType = TargetType::LocalWallet;
    cpu.coinbaseAddress = QStringLiteral("KTdHzRF4e71xcU1ea8tePLmZNijUnH6ZNYm");
    controller.addProfile(cpu);

    I18n::instance().setLanguage(I18n::Language::TR);
    MainWindow win(&store, &rpc, &controller, false);
    win.show();
    QApplication::processEvents();

    QTableWidget* table = win.findChild<QTableWidget*>();
    QVERIFY(table != nullptr);
    QHeaderView* hdr = table->horizontalHeader();
    QVERIFY(hdr != nullptr);

    const int initialCol3 = table->columnWidth(3);
    QVERIFY(initialCol3 > 120);

    hdr->resizeSection(3, 120);
    QApplication::processEvents();
    QCOMPARE(table->columnWidth(3), 120);

    WorkerProfile cpu2;
    cpu2.id = QStringLiteral("cpu_2");
    cpu2.name = QStringLiteral("worker_cpu_2_longer_name");
    cpu2.engine = MinerEngine::CpuMiner;
    cpu2.threads = 8;
    cpu2.targetType = TargetType::LocalWallet;
    cpu2.coinbaseAddress = QStringLiteral("KTdHzRF4e71xcU1ea8tePLmZNijUnH6ZNYm");
    controller.addProfile(cpu2);
    QApplication::processEvents();

    QCOMPARE(table->columnWidth(3), 120);
    QVERIFY(table->columnWidth(0) > 0);
    QVERIFY(table->columnWidth(7) > 0);

    Q_EMIT hdr->sectionHandleDoubleClicked(3);
    QApplication::processEvents();
    QVERIFY(table->columnWidth(3) >= initialCol3);
    verifyAllColumnsFit(table);
}

void TestUi::testAppIconIsSquareAndMultiSize()
{
    const QIcon icon = Theme::appIcon();
    QVERIFY(!icon.isNull());

    const QList<QSize> available = icon.availableSizes();
    static constexpr int kExpectedSizes[] = {16, 24, 32, 48, 64, 128, 256, 512};
    for (int sz : kExpectedSizes) {
        QVERIFY(available.contains(QSize(sz, sz)));
        const QPixmap pm = icon.pixmap(QSize(sz, sz));
        QVERIFY(!pm.isNull());
        QCOMPARE(pm.width(), pm.height());
        QVERIFY(pm.width() >= sz);
    }

    const QPixmap logoPm(QStringLiteral(":/logo.png"));
    QVERIFY(!logoPm.isNull());
    QCOMPARE(logoPm.size(), QSize(256, 256));

    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    ConfigStore store(tmpDir.path());
    RpcClient rpc;
    WorkerController controller(&store, &rpc);
    controller.loadInitialState(/*autoDiscoverIfEmpty=*/false);

    MainWindow win(&store, &rpc, &controller, false);
    const QIcon winIcon = win.windowIcon();
    QVERIFY(!winIcon.isNull());
    const QList<QSize> winSizes = winIcon.availableSizes();
    QVERIFY(winSizes.size() >= 8);
    for (const QSize& s : winSizes) {
        QCOMPARE(s.width(), s.height());
    }
}

void TestUi::testSelectionPreservedAcrossTableRefreshAndSorting()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    ConfigStore store(tmpDir.path());
    RpcClient rpc;
    WorkerController controller(&store, &rpc);
    controller.loadInitialState(/*autoDiscoverIfEmpty=*/false);

    auto makeProfile = [](const QString& id, const QString& name) {
        WorkerProfile p;
        p.id = id;
        p.name = name;
        p.engine = MinerEngine::CpuMiner;
        p.threads = 2;
        p.targetType = TargetType::LocalWallet;
        p.coinbaseAddress = QStringLiteral("KTdHzRF4e71xcU1ea8tePLmZNijUnH6ZNYm");
        return p;
    };

    controller.addProfile(makeProfile(QStringLiteral("id_alpha"), QStringLiteral("alpha")));
    controller.addProfile(makeProfile(QStringLiteral("id_bravo"), QStringLiteral("bravo")));
    controller.addProfile(makeProfile(QStringLiteral("id_charlie"), QStringLiteral("charlie")));

    I18n::instance().setLanguage(I18n::Language::TR);
    MainWindow win(&store, &rpc, &controller, false);
    win.show();
    QApplication::processEvents();

    QTableWidget* table = win.findChild<QTableWidget*>();
    QVERIFY(table != nullptr);
    QCOMPARE(table->rowCount(), 3);

    // 1. Unsorted refresh preserves selected worker across signals, theme, and language switches
    table->selectRow(1);
    QApplication::processEvents();
    QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
    QCOMPARE(table->item(1, 0)->text(), QStringLiteral("bravo"));

    Q_EMIT controller.workersChanged();
    Q_EMIT rpc.poolSeatsUpdated();
    win.setLightTheme(true);
    I18n::instance().setLanguage(I18n::Language::EN);
    QApplication::processEvents();

    QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
    QCOMPARE(table->selectionModel()->selectedRows().first().row(), 1);
    QCOMPARE(table->item(1, 0)->text(), QStringLiteral("bravo"));
    QCOMPARE(table->currentRow(), 1);

    // 2. Sorted descending refresh keeps selected worker by ID (charlie at visual row 0, not alpha)
    table->sortByColumn(0, Qt::DescendingOrder);
    QApplication::processEvents();
    QCOMPARE(table->item(0, 0)->text(), QStringLiteral("charlie"));

    table->selectRow(0);
    QApplication::processEvents();
    QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
    QCOMPARE(table->item(0, 0)->text(), QStringLiteral("charlie"));

    Q_EMIT controller.workersChanged();
    Q_EMIT rpc.poolSeatsUpdated();
    QApplication::processEvents();

    QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
    QCOMPARE(table->selectionModel()->selectedRows().first().row(), 0);
    QCOMPARE(table->item(0, 0)->text(), QStringLiteral("charlie"));
    QCOMPARE(table->currentRow(), 0);

    // 3. Profile insertion & removal while sorted
    controller.addProfile(makeProfile(QStringLiteral("id_delta"), QStringLiteral("delta")));
    QApplication::processEvents();
    QCOMPARE(table->rowCount(), 4);
    QCOMPARE(table->item(0, 0)->text(), QStringLiteral("delta"));
    QCOMPARE(table->item(1, 0)->text(), QStringLiteral("charlie"));
    QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
    QCOMPARE(table->selectionModel()->selectedRows().first().row(), 1);
    QCOMPARE(table->currentRow(), 1);

    QVERIFY(controller.removeProfile(QStringLiteral("id_charlie"), /*stopIfRunning=*/false));
    QApplication::processEvents();
    QCOMPARE(table->rowCount(), 3);
    QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
    QCOMPARE(table->selectionModel()->selectedRows().first().row(), 0);
    QCOMPARE(table->item(0, 0)->text(), QStringLiteral("delta"));
    QCOMPARE(table->currentRow(), 0);

    // 4. Filter hide/restore does not jump selection onto a different worker
    int bravoRow = -1;
    for (int r = 0; r < table->rowCount(); ++r) {
        if (table->item(r, 0)->text() == QStringLiteral("bravo")) {
            bravoRow = r;
            break;
        }
    }
    QVERIFY(bravoRow >= 0);
    table->selectRow(bravoRow);
    QApplication::processEvents();
    QCOMPARE(table->selectionModel()->selectedRows().size(), 1);

    QLineEdit* searchEdit = win.findChild<QLineEdit*>();
    QVERIFY(searchEdit != nullptr);
    searchEdit->setText(QStringLiteral("alpha"));
    QApplication::processEvents();
    Q_EMIT controller.workersChanged();
    QApplication::processEvents();
    QVERIFY(table->selectionModel()->selectedRows().isEmpty());

    searchEdit->clear();
    QApplication::processEvents();
    QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
    const int restoredRow = table->selectionModel()->selectedRows().first().row();
    QCOMPARE(table->item(restoredRow, 0)->text(), QStringLiteral("bravo"));
    QCOMPARE(table->currentRow(), restoredRow);
}

void TestUi::testFilterPillsAndWarningsFilter()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    ConfigStore store(tmpDir.path());
    RpcClient rpc;
    WorkerController controller(&store, &rpc);
    controller.loadInitialState(/*autoDiscoverIfEmpty=*/false);

    WorkerProfile alpha;
    alpha.id = QStringLiteral("id_alpha");
    alpha.name = QStringLiteral("alpha_healthy");
    alpha.engine = MinerEngine::CpuMiner;
    alpha.threads = 2;
    alpha.targetType = TargetType::RemoteSsh;
    alpha.coinbaseAddress = QStringLiteral("KTdHzRF4e71xcU1ea8tePLmZNijUnH6ZNYm");
    controller.addProfile(alpha);

    WorkerProfile beta;
    beta.id = QStringLiteral("id_beta");
    beta.name = QStringLiteral("beta_missing_addr");
    beta.engine = MinerEngine::CpuMiner;
    beta.threads = 2;
    beta.targetType = TargetType::RemoteSsh;
    beta.coinbaseAddress = QString();
    controller.addProfile(beta);

    WorkerProfile gamma;
    gamma.id = QStringLiteral("id_gamma");
    gamma.name = QStringLiteral("gamma_offline");
    gamma.engine = MinerEngine::CpuMiner;
    gamma.threads = 2;
    gamma.targetType = TargetType::CustomRpc;
    gamma.coinbaseAddress = QStringLiteral("KTdHzRF4e71xcU1ea8tePLmZNijUnH6ZNYm");
    controller.addProfile(gamma);

    I18n::instance().setLanguage(I18n::Language::EN);
    MainWindow win(&store, &rpc, &controller, false);
    win.show();
    QApplication::processEvents();

    QTableWidget* table = win.findChild<QTableWidget*>();
    QVERIFY(table != nullptr);
    QCOMPARE(table->rowCount(), 3);
    QVERIFY(table->item(2, 4)->text().contains(KTr(QStringLiteral("pool.rpc_offline"))));

    QList<QPushButton*> pills;
    for (QPushButton* b : win.findChildren<QPushButton*>()) {
        if (b->property("variant").toString() == QStringLiteral("pill")) {
            pills.append(b);
        }
    }
    QCOMPARE(pills.size(), 4);
    for (QPushButton* p : pills) {
        QVERIFY(p->isCheckable());
    }
    QVERIFY(pills[0]->isChecked());
    QVERIFY(pills[3]->text().contains(QStringLiteral("(2)")));

    pills[3]->click();
    QApplication::processEvents();
    QVERIFY(pills[3]->isChecked());
    QVERIFY(!pills[0]->isChecked());
    QVERIFY(table->isRowHidden(0));
    QVERIFY(!table->isRowHidden(1));
    QVERIFY(!table->isRowHidden(2));

    pills[0]->click();
    QApplication::processEvents();
    QVERIFY(pills[0]->isChecked());
    QVERIFY(!pills[3]->isChecked());
    QVERIFY(!table->isRowHidden(0));
    QVERIFY(!table->isRowHidden(1));
    QVERIFY(!table->isRowHidden(2));

    pills[1]->click();
    QApplication::processEvents();
    QVERIFY(pills[1]->isChecked());
    QVERIFY(table->isRowHidden(0));
    QVERIFY(table->isRowHidden(1));
    QVERIFY(table->isRowHidden(2));

    pills[2]->click();
    QApplication::processEvents();
    QVERIFY(pills[2]->isChecked());
    QVERIFY(!table->isRowHidden(0));
    QVERIFY(!table->isRowHidden(1));
    QVERIFY(!table->isRowHidden(2));
}

void TestUi::testToggleInternalGenButtonBidirectional()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    ConfigStore store(tmpDir.path());
    RpcClient rpc;
    WorkerController controller(&store, &rpc);
    controller.loadInitialState(/*autoDiscoverIfEmpty=*/false);

    WorkerProfile p1;
    p1.id = QStringLiteral("node_1");
    p1.name = QStringLiteral("worker_node_1");
    p1.engine = MinerEngine::CpuMiner;
    p1.threads = 2;
    p1.targetType = TargetType::CustomRpc;
    p1.rpcUrl = QStringLiteral("http://127.0.0.1:27998/");
    p1.coinbaseAddress = QStringLiteral("KTdHzRF4e71xcU1ea8tePLmZNijUnH6ZNYm");
    controller.addProfile(p1);

    WorkerProfile p2;
    p2.id = QStringLiteral("node_2");
    p2.name = QStringLiteral("worker_node_2");
    p2.engine = MinerEngine::CpuMiner;
    p2.threads = 2;
    p2.targetType = TargetType::CustomRpc;
    p2.rpcUrl = QStringLiteral("http://127.0.0.1:27999/");
    p2.coinbaseAddress = QStringLiteral("KT9XyZ8877665544332211aAbBcCdDeEfFg");
    controller.addProfile(p2);

    I18n::instance().setLanguage(I18n::Language::EN);
    MainWindow win(&store, &rpc, &controller, false);
    win.show();
    QApplication::processEvents();

    QPushButton* toggleGenBtn = nullptr;
    for (QPushButton* b : win.findChildren<QPushButton*>()) {
        if (b->text() == KTr(QStringLiteral("btn.enable_gen")) ||
            b->text() == KTr(QStringLiteral("btn.disable_gen"))) {
            toggleGenBtn = b;
            break;
        }
    }
    QVERIFY(toggleGenBtn != nullptr);

    QPushButton* warnPill = nullptr;
    int pillIdx = 0;
    for (QPushButton* b : win.findChildren<QPushButton*>()) {
        if (b->property("variant").toString() == QStringLiteral("pill")) {
            if (pillIdx == 3) warnPill = b;
            ++pillIdx;
        }
    }
    QVERIFY(warnPill != nullptr);

    // 1. Both targets have internalGenerate = true -> button offers Disable Internal Gen (EN & TR)
    NodeMiningStatus stOn;
    stOn.checked = true;
    stOn.reachable = true;
    stOn.internalGenerate = true;
    rpc.setTargetStatus(p1.targetKey(), stOn);
    rpc.setTargetStatus(p2.targetKey(), stOn);
    QApplication::processEvents();

    QCOMPARE(toggleGenBtn->text(), QStringLiteral("Disable Internal Gen"));
    QVERIFY(warnPill->text().contains(QStringLiteral("(0)")));
    QTableWidget* table = win.findChild<QTableWidget*>();
    QVERIFY(table != nullptr);
    for (int r = 0; r < table->rowCount(); ++r) {
        QVERIFY(!table->item(r, 4)->text().contains(QStringLiteral("2xGen")));
    }

    I18n::instance().setLanguage(I18n::Language::TR);
    QApplication::processEvents();
    QCOMPARE(toggleGenBtn->text(), QStringLiteral("Dahili Kazımı Kapat"));

    // 2. Both targets have internalGenerate = false -> button switches to Enable Internal Gen (TR & EN)
    NodeMiningStatus stOff;
    stOff.checked = true;
    stOff.reachable = true;
    stOff.internalGenerate = false;
    rpc.setTargetStatus(p1.targetKey(), stOff);
    rpc.setTargetStatus(p2.targetKey(), stOff);
    QApplication::processEvents();

    QCOMPARE(toggleGenBtn->text(), QStringLiteral("Dahili Kazımı Aç"));
    QVERIFY(warnPill->text().contains(QStringLiteral("(0)")));

    I18n::instance().setLanguage(I18n::Language::EN);
    QApplication::processEvents();
    QCOMPARE(toggleGenBtn->text(), QStringLiteral("Enable Internal Gen"));

    // 3. Partial state (one target true, one false) -> button switches back to Disable Internal Gen
    rpc.setTargetStatus(p1.targetKey(), stOn);
    QApplication::processEvents();
    QCOMPARE(toggleGenBtn->text(), QStringLiteral("Disable Internal Gen"));
    QVERIFY(warnPill->text().contains(QStringLiteral("(0)")));

    I18n::instance().setLanguage(I18n::Language::TR);
    QApplication::processEvents();
    QCOMPARE(toggleGenBtn->text(), QStringLiteral("Dahili Kazımı Kapat"));
}

void TestUi::testAutoStopDeferredCheckboxAndI18n()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    QSettings settings(QStringLiteral("KristaTech"), QStringLiteral("MinerManager"));
    const QVariant prevVal = settings.value(QStringLiteral("behavior/autoStopDeferred"));
    settings.setValue(QStringLiteral("behavior/autoStopDeferred"), false);

    ConfigStore store(tmpDir.path());
    RpcClient rpc;
    WorkerController controller(&store, &rpc);
    controller.loadInitialState(/*autoDiscoverIfEmpty=*/false);

    I18n::instance().setLanguage(I18n::Language::EN);
    MainWindow win(&store, &rpc, &controller, false);
    win.show();
    QApplication::processEvents();

    QCheckBox* chk = win.findChild<QCheckBox*>(QStringLiteral("chkAutoStopDeferred"));
    QVERIFY(chk != nullptr);
    QCOMPARE(chk->isChecked(), false);
    QCOMPARE(controller.autoStopOnDeferred(), false);
    QCOMPARE(chk->text(), QStringLiteral("Auto-Stop Deferred"));

    chk->click();
    QApplication::processEvents();
    QCOMPARE(chk->isChecked(), true);
    QCOMPARE(controller.autoStopOnDeferred(), true);
    QCOMPARE(settings.value(QStringLiteral("behavior/autoStopDeferred")).toBool(), true);

    chk->click();
    QApplication::processEvents();
    QCOMPARE(chk->isChecked(), false);
    QCOMPARE(controller.autoStopOnDeferred(), false);
    QCOMPARE(settings.value(QStringLiteral("behavior/autoStopDeferred")).toBool(), false);

    controller.setAutoStopOnDeferred(true);
    QApplication::processEvents();
    QCOMPARE(chk->isChecked(), true);

    I18n::instance().setLanguage(I18n::Language::TR);
    QApplication::processEvents();
    QCOMPARE(chk->text(), QStringLiteral("Erteleneni Oto-Durdur"));

    I18n::instance().setLanguage(I18n::Language::EN);
    if (prevVal.isValid()) {
        settings.setValue(QStringLiteral("behavior/autoStopDeferred"), prevVal);
    } else {
        settings.remove(QStringLiteral("behavior/autoStopDeferred"));
    }
}

void TestUi::testActionButtonsNotClippedWithVerticalScrollbar()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

#ifndef Q_OS_WIN
    const QString mockMiner = tmpDir.filePath(QStringLiteral("mock_live_miner.sh"));
    {
        QFile f(mockMiner);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("#!/bin/sh\n"
                "echo \"[2026-11-20 00:22:04] ADAM Auto-Register deferred: This wallet already holds an active 10-KRISTA PoW-Lock seat\"\n"
                "echo \"[2026-11-20 00:22:05] New Block 6034, Tx 0, Net Diff 0.001801\"\n"
                "echo \"[2026-11-20 00:22:06] Miner TTF @ 219.40 kh/s 12s, Net TTF @ 20.00 Mh/s 0s\"\n"
                "echo \"[2026-11-20 00:22:07] CPU temp: curr 31 C max 33\"\n"
                "echo \"[2026-11-20 00:22:08] 48 A48 S0 R0 BLOCK SOLVED 48, 12.3 sec (1ms)\"\n"
                "exec sleep 30\n");
        f.close();
        QVERIFY(f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                                 | QFileDevice::ReadGroup | QFileDevice::ExeGroup
                                 | QFileDevice::ReadOther | QFileDevice::ExeOther));
    }
#endif

    ConfigStore store(tmpDir.filePath(QStringLiteral("cfg")));
    RpcClient rpc;
    WorkerController controller(&store, &rpc);
    controller.loadInitialState(/*autoDiscoverIfEmpty=*/false);

    for (int i = 1; i <= 17; ++i) {
        WorkerProfile p;
        p.id = QStringLiteral("worker_%1").arg(i, 2, 10, QLatin1Char('0'));
        if (i <= 7) {
            p.name = QStringLiteral("Local cpuminer (KTdHzRF%1)").arg(i);
            p.targetType = (i == 1) ? TargetType::CustomRpc : TargetType::LocalWallet;
            if (i == 1) {
#ifndef Q_OS_WIN
                p.binaryPath = mockMiner;
#endif
                p.rpcUrl = QStringLiteral("http://127.0.0.1:27998/");
                p.rpcUser = QStringLiteral("u");
                p.rpcPassword = QStringLiteral("p");
            } else {
                p.confPath = QStringLiteral("/home/user/.kristatech/kristatech.conf");
            }
        } else {
            p.name = QStringLiteral("krista-cold-%1 (KTXWGMQ%2)").arg(i - 7, 2, 10, QLatin1Char('0')).arg(i - 7);
            p.targetType = TargetType::PodmanContainer;
            p.podmanName = QStringLiteral("krista-cold-%1").arg(i - 7, 2, 10, QLatin1Char('0'));
        }
        p.engine = MinerEngine::CpuMiner;
        p.threads = 1;
        p.coinbaseAddress = QStringLiteral("KTdHzRF4e71xcU1ea8tePLmZNijUnH%1ZNYm").arg(i, 3, 10, QLatin1Char('0'));
        controller.addProfile(p);
    }

    I18n::instance().setLanguage(I18n::Language::EN);
    MainWindow win(&store, &rpc, &controller, true);
    win.show();
    QApplication::processEvents();

    // Simulate live RPC & wide running worker statuses after initial show
    NodeMiningStatus unreachable;
    unreachable.checked = true;
    unreachable.reachable = false;
    rpc.setTargetStatus(QStringLiteral("local:/home/user/.kristatech/kristatech.conf"), unreachable);
    rpc.setTargetStatus(QStringLiteral("rpc:http://127.0.0.1:27998/"), unreachable);
    for (int i = 1; i <= 10; ++i) {
        rpc.setTargetStatus(QStringLiteral("podman:krista-cold-%1").arg(i, 2, 10, QLatin1Char('0')), unreachable);
    }
    Q_EMIT rpc.poolSeatsUpdated();

#ifndef Q_OS_WIN
    controller.startWorker(QStringLiteral("worker_01"));
    QTRY_VERIFY_WITH_TIMEOUT(controller.runtimeById(QStringLiteral("worker_01")).hashrateKhs > 200.0
                             && controller.runtimeById(QStringLiteral("worker_01")).warningDeferred, 4000);
#endif
    QApplication::processEvents();

    QTableWidget* table = win.findChild<QTableWidget*>();
    QVERIFY(table != nullptr);

    auto checkViewportAndButtons = [&](const char* langLabel) {
        QApplication::processEvents();
        verifyAllColumnsFit(table);

        // Verify full coinbase address remains searchable & preserved in tooltip even though cell is abbreviated
        QCOMPARE(table->item(0, 3)->text().contains(QStringLiteral("…")), true);
        QCOMPARE(table->item(0, 3)->toolTip(), QStringLiteral("KTdHzRF4e71xcU1ea8tePLmZNijUnH001ZNYm"));

        int totalColsWidth = 0;
        for (int c = 0; c < table->columnCount(); ++c) {
            totalColsWidth += table->columnWidth(c);
        }
        const QByteArray fitMsg = QStringLiteral("[%1] Table totalColsWidth %2 > viewport width %3 (win width %4)")
            .arg(QLatin1String(langLabel)).arg(totalColsWidth).arg(table->viewport()->width()).arg(win.width()).toUtf8();
        QVERIFY2(totalColsWidth <= table->viewport()->width(), fitMsg.constData());

        for (int r = 0; r < table->rowCount(); ++r) {
            QWidget* cw = table->cellWidget(r, 7);
            QVERIFY(cw != nullptr);
            QVERIFY2(cw->width() <= table->columnWidth(7),
                     qPrintable(QStringLiteral("[%1] Row %2 cellWidget width %3 > col 7 width %4")
                         .arg(QLatin1String(langLabel)).arg(r).arg(cw->width()).arg(table->columnWidth(7))));
            const auto btns = cw->findChildren<QPushButton*>();
            QCOMPARE(btns.size(), 4);
            for (QPushButton* btn : btns) {
                QVERIFY2(btn->geometry().right() <= cw->width() - 1,
                         qPrintable(QStringLiteral("[%1] Row %2 button '%3' right %4 overflows cellWidget width %5")
                             .arg(QLatin1String(langLabel)).arg(r).arg(btn->text())
                             .arg(btn->geometry().right()).arg(cw->width())));
            }
        }
    };

    checkViewportAndButtons("EN");

    I18n::instance().setLanguage(I18n::Language::TR);
    checkViewportAndButtons("TR");

    I18n::instance().setLanguage(I18n::Language::EN);
#ifndef Q_OS_WIN
    controller.stopWorker(QStringLiteral("worker_01"));
#endif
}

void TestUi::testWorkerDialogCustomRpcAndRemoteSshVisibilityAndValidation()
{
    I18n::instance().setLanguage(I18n::Language::EN);
    RpcClient rpc;
    WorkerDialog dlg(nullptr, &rpc, {}, {});
    dlg.show();
    QApplication::processEvents();

    QComboBox* comboTarget = nullptr;
    for (QComboBox* cb : dlg.findChildren<QComboBox*>()) {
        if (cb->findData(static_cast<int>(TargetType::RemoteSsh)) >= 0) {
            comboTarget = cb;
            break;
        }
    }
    QVERIFY(comboTarget != nullptr);

    QLineEdit* editName = nullptr;
    QLineEdit* editBinary = nullptr;
    QLineEdit* editConf = nullptr;
    QLineEdit* editRpcUrl = nullptr;
    QLineEdit* editRpcUser = nullptr;
    QLineEdit* editRpcPass = nullptr;
    QLineEdit* editSshTarget = nullptr;
    QLineEdit* editRemoteBin = nullptr;
    QSpinBox* spinSshPort = nullptr;

    for (QLineEdit* le : dlg.findChildren<QLineEdit*>()) {
        const QString ph = le->placeholderText();
        if (ph.contains(QStringLiteral("worker_gpu_0"))) editName = le;
        else if (ph == KTr(QStringLiteral("dlg.rpc_user"))) editRpcUser = le;
        else if (ph == KTr(QStringLiteral("dlg.rpc_pass"))) editRpcPass = le;
        else if (ph == QStringLiteral("user@server-ip")) editSshTarget = le;
        else if (ph.contains(QStringLiteral("/usr/local/bin/cpuminer"))) editRemoteBin = le;
        else if (le->text() == QStringLiteral("http://127.0.0.1:27998/")) editRpcUrl = le;
        else if (le->text() == ProcessScanner::defaultCpuMinerPath()) editBinary = le;
        else if (le->text() == ConfigStore::defaultKristaTechConfPath()) editConf = le;
    }
    for (QSpinBox* sb : dlg.findChildren<QSpinBox*>()) {
        if (sb->prefix() == QStringLiteral("Port: ")) {
            spinSshPort = sb;
            break;
        }
    }

    QVERIFY(editName && editBinary && editConf && editRpcUrl && editRpcUser && editRpcPass
            && editSshTarget && editRemoteBin && spinSshPort);

    QFormLayout* form = nullptr;
    for (QFormLayout* fl : dlg.findChildren<QFormLayout*>()) {
        if (fl->labelForField(editRpcUrl->parentWidget())) {
            form = fl;
            break;
        }
    }
    QVERIFY(form != nullptr);

    QWidget* rowBinary = editBinary->parentWidget();
    QWidget* rowConf = editConf->parentWidget();
    QWidget* rowRpcUrl = editRpcUrl->parentWidget();
    QWidget* rowRpcAuth = editRpcUser->parentWidget();
    QWidget* rowSsh = editSshTarget->parentWidget();
    QWidget* rowPodman = form->labelForField(rowConf) ? nullptr : nullptr;
    for (QComboBox* cb : dlg.findChildren<QComboBox*>()) {
        if (cb->isEditable() && cb->parentWidget() != &dlg && form->labelForField(cb->parentWidget())) {
            rowPodman = cb->parentWidget();
            break;
        }
    }
    QVERIFY(rowPodman != nullptr);

    auto assertRowVis = [&](QWidget* row, bool expectedVisible, const char* name) {
        QWidget* lbl = form->labelForField(row);
        QVERIFY2(lbl != nullptr, name);
        QCOMPARE(row->isVisibleTo(&dlg), expectedVisible);
        QCOMPARE(lbl->isVisibleTo(&dlg), expectedVisible);
    };

    // 1. LocalWallet
    comboTarget->setCurrentIndex(comboTarget->findData(static_cast<int>(TargetType::LocalWallet)));
    QApplication::processEvents();
    assertRowVis(rowBinary, true, "local:binary");
    assertRowVis(rowConf, true, "local:conf");
    assertRowVis(rowPodman, false, "local:podman");
    assertRowVis(rowRpcUrl, false, "local:rpcUrl");
    assertRowVis(rowRpcAuth, false, "local:rpcAuth");
    assertRowVis(rowSsh, false, "local:ssh");

    // 2. PodmanContainer
    comboTarget->setCurrentIndex(comboTarget->findData(static_cast<int>(TargetType::PodmanContainer)));
    QApplication::processEvents();
    assertRowVis(rowBinary, true, "podman:binary");
    assertRowVis(rowConf, false, "podman:conf");
    assertRowVis(rowPodman, true, "podman:podman");
    assertRowVis(rowRpcUrl, false, "podman:rpcUrl");
    assertRowVis(rowRpcAuth, false, "podman:rpcAuth");
    assertRowVis(rowSsh, false, "podman:ssh");

    // 3. CustomRpc
    comboTarget->setCurrentIndex(comboTarget->findData(static_cast<int>(TargetType::CustomRpc)));
    QApplication::processEvents();
    assertRowVis(rowBinary, true, "custom:binary");
    assertRowVis(rowConf, false, "custom:conf");
    assertRowVis(rowPodman, false, "custom:podman");
    assertRowVis(rowRpcUrl, true, "custom:rpcUrl");
    assertRowVis(rowRpcAuth, true, "custom:rpcAuth");
    assertRowVis(rowSsh, false, "custom:ssh");

    // 4. RemoteSsh (binary row + label MUST both hide)
    comboTarget->setCurrentIndex(comboTarget->findData(static_cast<int>(TargetType::RemoteSsh)));
    QApplication::processEvents();
    assertRowVis(rowBinary, false, "ssh:binary");
    assertRowVis(rowConf, false, "ssh:conf");
    assertRowVis(rowPodman, false, "ssh:podman");
    assertRowVis(rowRpcUrl, true, "ssh:rpcUrl");
    assertRowVis(rowRpcAuth, true, "ssh:rpcAuth");
    assertRowVis(rowSsh, true, "ssh:ssh");

    // 5. Validation + normalization on CustomRpc and RemoteSsh
    QPushButton* btnSave = nullptr;
    for (QPushButton* b : dlg.findChildren<QPushButton*>()) {
        if (b->property("variant").toString() == QStringLiteral("primary")) {
            btnSave = b;
            break;
        }
    }
    QVERIFY(btnSave != nullptr);

    editName->setText(QStringLiteral("Custom Rig"));
    comboTarget->setCurrentIndex(comboTarget->findData(static_cast<int>(TargetType::CustomRpc)));
    editRpcUrl->setText(QStringLiteral("ftp://invalid-url"));
    editRpcUser->setText(QStringLiteral("u1"));
    editRpcPass->setText(QStringLiteral("p1"));
    QTimer::singleShot(50, qApp, []() {
        if (QWidget* w = QApplication::activeModalWidget()) w->close();
    });
    btnSave->click();
    QApplication::processEvents();
    QCOMPARE(dlg.result(), 0); // Rejected by validation

    editRpcUrl->setText(QStringLiteral(" 192.168.1.50:27998 "));
    const WorkerProfile customRes = dlg.resultProfile();
    QCOMPARE(customRes.rpcUrl, QStringLiteral("http://192.168.1.50:27998/"));
    QCOMPARE(customRes.rpcUser, QStringLiteral("u1"));
    QCOMPARE(customRes.rpcPassword, QStringLiteral("p1"));

    comboTarget->setCurrentIndex(comboTarget->findData(static_cast<int>(TargetType::RemoteSsh)));
    editSshTarget->setText(QStringLiteral("-oProxyCommand=evil"));
    editRpcUrl->setText(QStringLiteral("127.0.0.1:27998"));
    editRpcUser->setText(QStringLiteral("u2"));
    editRpcPass->setText(QStringLiteral("p2"));
    QTimer::singleShot(50, qApp, []() {
        if (QWidget* w = QApplication::activeModalWidget()) w->close();
    });
    btnSave->click();
    QApplication::processEvents();
    QCOMPARE(dlg.result(), 0); // Rejected bad SSH target

    editSshTarget->setText(QStringLiteral("miner@10.0.0.5"));
    spinSshPort->setValue(2222);
    editRemoteBin->setText(QStringLiteral("~/bin/cpuminer"));
    btnSave->click();
    QApplication::processEvents();
    QCOMPARE(dlg.result(), static_cast<int>(QDialog::Accepted));

    const WorkerProfile sshRes = dlg.resultProfile();
    QCOMPARE(sshRes.targetType, TargetType::RemoteSsh);
    QCOMPARE(sshRes.sshTarget, QStringLiteral("miner@10.0.0.5"));
    QCOMPARE(sshRes.sshPort, 2222);
    QCOMPARE(sshRes.remoteBinaryPath, QStringLiteral("~/bin/cpuminer"));
    QCOMPARE(sshRes.rpcUrl, QStringLiteral("http://127.0.0.1:27998/"));
    QCOMPARE(sshRes.rpcUser, QStringLiteral("u2"));
    QCOMPARE(sshRes.rpcPassword, QStringLiteral("p2"));
}

void TestUi::testFileDialogAndItemViewsThemeContrast()
{
    for (const bool isLight : {false, true}) {
        Theme::applyTheme(qApp, isLight);
        const QPalette pal = qApp->palette();

        const int baseL = pal.color(QPalette::Base).lightness();
        const int altBaseL = pal.color(QPalette::AlternateBase).lightness();
        const int winL = pal.color(QPalette::Window).lightness();
        const int textL = pal.color(QPalette::Text).lightness();
        const int winTextL = pal.color(QPalette::WindowText).lightness();
        const int btnTextL = pal.color(QPalette::ButtonText).lightness();
        const int hlTextL = pal.color(QPalette::HighlightedText).lightness();

        if (!isLight) {
            QVERIFY(baseL < 60);
            QVERIFY(altBaseL < 60);
            QVERIFY(winL < 60);
            QVERIFY(textL > 190);
            QVERIFY(winTextL > 190);
            QVERIFY(btnTextL > 190);
            QVERIFY(hlTextL > 190);
        } else {
            QVERIFY(baseL > 220);
            QVERIFY(altBaseL > 220);
            QVERIFY(winL > 220);
            QVERIFY(textL < 70);
            QVERIFY(winTextL < 70);
            QVERIFY(btnTextL < 70);
            QVERIFY(hlTextL < 70);
        }

        const QString qss = Theme::buildStylesheet(isLight);
        QVERIFY(qss.contains(QStringLiteral("QTreeView")));
        QVERIFY(qss.contains(QStringLiteral("QListView")));
        QVERIFY(qss.contains(QStringLiteral("QTableView")));
        QVERIFY(qss.contains(QStringLiteral("QToolButton")));

        QFileDialog fd;
        fd.setOption(QFileDialog::DontUseNativeDialog, true);
        fd.ensurePolished();
        QApplication::processEvents();

        const auto trees = fd.findChildren<QTreeView*>();
        const auto lists = fd.findChildren<QListView*>();
        QVERIFY(!trees.isEmpty());
        QVERIFY(!lists.isEmpty());
        for (QTreeView* tv : trees) {
            const QPalette p = tv->palette();
            QCOMPARE(p.color(QPalette::Base), pal.color(QPalette::Base));
            QCOMPARE(p.color(QPalette::Text), pal.color(QPalette::Text));
        }
        for (QListView* lv : lists) {
            const QPalette p = lv->palette();
            QCOMPARE(p.color(QPalette::Base), pal.color(QPalette::Base));
            QCOMPARE(p.color(QPalette::Text), pal.color(QPalette::Text));
        }
    }

    Theme::applyTheme(qApp, false);
}

void TestUi::testFairnessWalletLockAndSeatExpiryUi()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    ConfigStore store(tmpDir.path());
    RpcClient rpc;
    WorkerController controller(&store, &rpc);
    controller.loadInitialState(/*autoDiscoverIfEmpty=*/false);

    auto makeCustom = [&](const QString& id, const QString& name, const QString& url, const QString& addr) {
        WorkerProfile p;
        p.id = id;
        p.name = name;
        p.engine = MinerEngine::CpuMiner;
        p.threads = 1;
        p.targetType = TargetType::CustomRpc;
        p.rpcUrl = url;
        p.rpcUser = QStringLiteral("u");
        p.rpcPassword = QStringLiteral("p");
        p.coinbaseAddress = addr;
        controller.addProfile(p);
        return p;
    };

    const WorkerProfile pMn = makeCustom(
        QStringLiteral("w_mn"), QStringLiteral("01_masternode"),
        QStringLiteral("http://127.0.0.1:28001/"), QStringLiteral("KTMnAddr00000000000000000000000001"));
    const WorkerProfile pPowExp = makeCustom(
        QStringLiteral("w_pow_exp"), QStringLiteral("02_pow_expiring"),
        QStringLiteral("http://127.0.0.1:28002/"), QStringLiteral("KTPowExp0000000000000000000000001"));
    const WorkerProfile pZeroExp = makeCustom(
        QStringLiteral("w_zero_exp"), QStringLiteral("03_zero_expiring"),
        QStringLiteral("http://127.0.0.1:28002/"), QStringLiteral("KTZeroExp000000000000000000000001"));
    const WorkerProfile pLocked = makeCustom(
        QStringLiteral("w_locked"), QStringLiteral("04_wallet_locked"),
        QStringLiteral("http://127.0.0.1:28003/"), QStringLiteral("KTLockCl0000000000000000000000001"));

    I18n::instance().setLanguage(I18n::Language::EN);
    MainWindow win(&store, &rpc, &controller, false);
    win.show();
    // Drain initial async refreshTargets() connection-refused probes from MainWindow ctor before injecting mock states
    QTRY_VERIFY_WITH_TIMEOUT(
        rpc.statusForTarget(pMn.targetKey()).checked
        && rpc.statusForTarget(pPowExp.targetKey()).checked
        && rpc.statusForTarget(pLocked.targetKey()).checked,
        3000);

    ConsensusStatus cs;
    cs.known = true;
    cs.chain = QStringLiteral("main");
    cs.fairnessActive = true;
    cs.modelDActive = true;
    cs.activeUpgrades = {QStringLiteral("Fairness"), QStringLiteral("Model D")};
    rpc.setConsensusStatus(cs);

    NodeMiningStatus nsUnlocked;
    nsUnlocked.checked = true;
    nsUnlocked.reachable = true;
    nsUnlocked.blockHeight = 9000;
    nsUnlocked.chain = QStringLiteral("main");
    nsUnlocked.walletChecked = true;
    nsUnlocked.walletLocked = false;
    rpc.setTargetStatus(pMn.targetKey(), nsUnlocked);
    rpc.setTargetStatus(pPowExp.targetKey(), nsUnlocked);

    NodeMiningStatus nsLocked = nsUnlocked;
    nsLocked.walletEncrypted = true;
    nsLocked.walletLocked = true;
    rpc.setTargetStatus(pLocked.targetKey(), nsLocked);

    QHash<QString, PoolSeatInfo> seats;
    seats.insert(pMn.coinbaseAddress, PoolSeatInfo{true, true, QStringLiteral("masternode"), 0, 0, 9000});
    seats.insert(pPowExp.coinbaseAddress, PoolSeatInfo{true, true, QStringLiteral("pow_lock"), 9120, 120, 9000});
    seats.insert(pZeroExp.coinbaseAddress, PoolSeatInfo{true, true, QStringLiteral("zerocoin_pow"), 9018, 18, 9000});
    seats.insert(pLocked.coinbaseAddress, PoolSeatInfo{true, true, QStringLiteral("coin_lock"), 11500, 2500, 9000});
    rpc.setPoolSeatsForTest(seats, 9000);
    QApplication::processEvents();

    QTableWidget* table = win.findChild<QTableWidget*>();
    QVERIFY(table != nullptr);
    QCOMPARE(table->rowCount(), 4);

    // Masternode has no bogus (0b), expiring tracks show warning badge, locked node shows Wallet Locked
    QCOMPARE(table->item(0, 4)->text(), QStringLiteral("✓ masternode"));
    QCOMPARE(table->item(1, 4)->text(), QStringLiteral("⚠ pow_lock (120b)"));
    QCOMPARE(table->item(2, 4)->text(), QStringLiteral("⚠ zerocoin_pow (18b)"));
    QVERIFY(table->item(3, 4)->text().contains(KTr(QStringLiteral("pool.wallet_locked"))));

    // Warnings pill counts rows 1, 2, 3 (not healthy masternode row 0)
    QPushButton* warnPill = nullptr;
    int pillIdx = 0;
    for (QPushButton* b : win.findChildren<QPushButton*>()) {
        if (b->property("variant").toString() == QStringLiteral("pill")) {
            if (pillIdx == 3) warnPill = b;
            ++pillIdx;
        }
    }
    QVERIFY(warnPill != nullptr);
    QVERIFY(warnPill->text().contains(QStringLiteral("(3)")));

    QFrame* warnBanner = win.findChild<QFrame*>(QStringLiteral("warningBanner"));
    QPushButton* btnRenew = win.findChild<QPushButton*>(QStringLiteral("btnRenewSeat"));
    QVERIFY(warnBanner != nullptr);
    QVERIFY(btnRenew != nullptr);

    // Selecting expiring pow_lock shows banner + Renew Seat button
    table->selectRow(1);
    QApplication::processEvents();
    QVERIFY(warnBanner->isVisibleTo(&win));
    QVERIFY(btnRenew->isVisibleTo(&win));

    // Selecting locked-wallet worker shows locked warning and hides Renew Seat button
    table->selectRow(3);
    QApplication::processEvents();
    QVERIFY(warnBanner->isVisibleTo(&win));
    QVERIFY(!btnRenew->isVisibleTo(&win));

    // Network KPI surfaces Fairness consensus badge + Locked Wallet alert in EN & TR
    QList<QFrame*> kpiCards;
    for (QFrame* f : win.findChildren<QFrame*>()) {
        if (f->objectName() == QStringLiteral("kpiCard")) kpiCards.append(f);
    }
    QCOMPARE(kpiCards.size(), 6);
    QLabel* netVal = kpiCards[5]->findChild<QLabel*>(QStringLiteral("kpiValue"));
    QLabel* netSub = kpiCards[5]->findChild<QLabel*>(QStringLiteral("kpiSub"));
    QVERIFY(netVal != nullptr);
    QVERIFY(netSub != nullptr);
    QCOMPARE(netVal->text(), QStringLiteral("Block #9000 • Fairness"));
    QCOMPARE(netSub->text(), QStringLiteral("🔒 Wallet Locked (1 node(s))"));

    I18n::instance().setLanguage(I18n::Language::TR);
    QApplication::processEvents();
    QCOMPARE(netSub->text(), QStringLiteral("🔒 Cüzdan Kilitli (1 düğüm)"));
    QVERIFY(table->item(3, 4)->text().contains(QStringLiteral("Cüzdan Kilitli")));
    I18n::instance().setLanguage(I18n::Language::EN);

    // WorkerDialog expiring-seat warning visibility check
    WorkerDialog dlg(&pPowExp, &rpc, {}, {});
    dlg.show();
    QApplication::processEvents();
    QFrame* dlgWarn = dlg.findChild<QFrame*>(QStringLiteral("warningBanner"));
    QVERIFY(dlgWarn != nullptr);
    QVERIFY(dlgWarn->isVisibleTo(&dlg));
}

void TestUi::testSecurityWarningsAndImportGuard()
{
    I18n::instance().setLanguage(I18n::Language::EN);
    RpcClient rpc;
    WorkerDialog dlg(nullptr, &rpc, {}, {});
    dlg.show();
    QApplication::processEvents();

    QComboBox* comboTarget = nullptr;
    QComboBox* comboPodman = nullptr;
    for (QComboBox* cb : dlg.findChildren<QComboBox*>()) {
        if (cb->findData(static_cast<int>(TargetType::RemoteSsh)) >= 0) {
            comboTarget = cb;
        } else if (!comboPodman && cb->isEditable() && cb->parentWidget() != &dlg) {
            comboPodman = cb;
        }
    }
    QVERIFY(comboTarget != nullptr);
    QVERIFY(comboPodman != nullptr);

    QLineEdit* editName = nullptr;
    QLineEdit* editRpcUrl = nullptr;
    for (QLineEdit* le : dlg.findChildren<QLineEdit*>()) {
        if (le->placeholderText().contains(QStringLiteral("worker_gpu_0"))) editName = le;
        else if (le->text() == QStringLiteral("http://127.0.0.1:27998/")) editRpcUrl = le;
    }
    QVERIFY(editName != nullptr);
    QVERIFY(editRpcUrl != nullptr);

    QPushButton* btnSave = nullptr;
    for (QPushButton* b : dlg.findChildren<QPushButton*>()) {
        if (b->property("variant").toString() == QStringLiteral("primary")) {
            btnSave = b;
            break;
        }
    }
    QVERIFY(btnSave != nullptr);

    // 1. Invalid Podman container name ("--privileged") is rejected on Save
    editName->setText(QStringLiteral("Bad Podman"));
    comboTarget->setCurrentIndex(comboTarget->findData(static_cast<int>(TargetType::PodmanContainer)));
    comboPodman->setEditText(QStringLiteral("--privileged"));
    QTimer::singleShot(50, qApp, []() {
        if (QWidget* w = QApplication::activeModalWidget()) w->close();
    });
    btnSave->click();
    QApplication::processEvents();
    QCOMPARE(dlg.result(), 0);

    // 2. Unencrypted non-loopback http:// CustomRpc displays inline warning banner
    QFrame* warnFrame = dlg.findChild<QFrame*>(QStringLiteral("warningBanner"));
    QLabel* warnLabel = dlg.findChild<QLabel*>(QStringLiteral("warnLabel"));
    QVERIFY(warnFrame != nullptr);
    QVERIFY(warnLabel != nullptr);

    comboTarget->setCurrentIndex(comboTarget->findData(static_cast<int>(TargetType::CustomRpc)));
    editRpcUrl->setText(QStringLiteral("http://198.51.100.10:27998/"));
    QApplication::processEvents();
    QVERIFY(warnFrame->isVisibleTo(&dlg));
    QVERIFY(warnLabel->text().contains(QStringLiteral("http://")));

    // 3. Switching to HTTPS or loopback HTTP clears the insecure HTTP warning
    editRpcUrl->setText(QStringLiteral("https://198.51.100.10:27998/"));
    QApplication::processEvents();
    QVERIFY(!warnFrame->isVisibleTo(&dlg));

    editRpcUrl->setText(QStringLiteral("http://127.0.0.1:27998/"));
    QApplication::processEvents();
    QVERIFY(!warnFrame->isVisibleTo(&dlg));
}

QTEST_MAIN(TestUi)
#include "test_ui.moc"
