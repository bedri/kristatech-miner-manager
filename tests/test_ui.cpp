#include "config_store.h"
#include "i18n.h"
#include "main_window.h"
#include "models.h"
#include "rpc_client.h"
#include "theme.h"
#include "worker_controller.h"

#include <QApplication>
#include <QFontMetrics>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLayout>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>
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

private:
    QTemporaryDir m_settingsDir;
};

void TestUi::initTestCase()
{
    QVERIFY(m_settingsDir.isValid());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, m_settingsDir.path());
    qApp->setApplicationName(QStringLiteral("KristaTechMinerManagerTest"));
    qApp->setOrganizationName(QStringLiteral("KristaTechTest"));
    qApp->setStyleSheet(Theme::buildStylesheet(false));
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

QTEST_MAIN(TestUi)
#include "test_ui.moc"
