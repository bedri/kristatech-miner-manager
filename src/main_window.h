#pragma once

#include "models.h"

#include <QMainWindow>
#include <QSet>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVector>

class ConfigStore;
class ProcessScanner;
class QAction;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QShowEvent;
class QTableWidget;
class RpcClient;
class WorkerController;

/**
 * Main desktop control plane for managing cpuminer and ccminer worker fleets.
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(ConfigStore* store,
                        RpcClient* rpc,
                        WorkerController* controller,
                        bool initialLightTheme,
                        QWidget* parent = nullptr);

    void setLightTheme(bool isLight);

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private Q_SLOTS:
    void refreshTableAndKpis();
    void onWorkerLogAppended(const QString& id, const QStringList& newLines);
    void onAddWorker();
    void onEditWorker(const QString& id);
    void onDeleteWorker(const QString& id);
    void onDiscoverFleet();
    void onStopUnregistered();
    void onToggleAllInternalGen();
    void onImportProfiles();
    void onExportProfiles();
    void onToggleTheme();
    void onFilterChanged();
    void onRowSelectionChanged();
    void requestQuit();

private:
    void setupUi();
    void setupTray();
    void retranslateUi();
    void rebuildTable();
    void applyFilters();
    void updateKpis();
    void updateInternalGenButton();
    void updateInspector();
    void pollGpusAndPods();
    int computeColumnFitWidth(int col) const;
    void fitTableColumnToContents(int col, bool allowShrink);
    void fitTableColumnsToContents(bool allowShrink);
    void ensureInitialWindowFitsTable();
    void restoreTableSelection(const QString& targetWorkerId, int preferredCol);

    ConfigStore* m_store{nullptr};
    RpcClient* m_rpc{nullptr};
    WorkerController* m_controller{nullptr};
    ProcessScanner* m_scanner{nullptr};

    bool m_isLight{false};
    bool m_reallyQuitting{false};
    bool m_trayNotifiedOnce{false};
    bool m_initialColumnFitDone{false};
    bool m_autoResizingColumns{false};
    bool m_rebuildingTable{false};
    QSet<int> m_userResizedColumns;
    QString m_lastProfileSignature;
    int m_statusFilter{0}; // 0=All, 1=Running, 2=Stopped, 3=Warnings
    QString m_selectedWorkerId;

    QVector<GpuTelemetry> m_gpus;
    QVector<PodmanContainerInfo> m_pods;

    QTimer m_rpcTimer;
    QTimer m_hwTimer;

    // Header & toolbar
    QLabel* m_lblTitle{nullptr};
    QLabel* m_lblSubtitle{nullptr};
    QPushButton* m_btnAdd{nullptr};
    QPushButton* m_btnDiscover{nullptr};
    QPushButton* m_btnStartAll{nullptr};
    QPushButton* m_btnStopAll{nullptr};
    QPushButton* m_btnStopUnreg{nullptr};
    QPushButton* m_btnDisableGen{nullptr};
    QPushButton* m_btnImport{nullptr};
    QPushButton* m_btnExport{nullptr};
    QPushButton* m_btnLang{nullptr};
    QPushButton* m_btnTheme{nullptr};

    // KPI Cards
    QLabel* m_kpiTitleWorkers{nullptr};
    QLabel* m_kpiValWorkers{nullptr};
    QLabel* m_kpiSubWorkers{nullptr};

    QLabel* m_kpiTitlePool{nullptr};
    QLabel* m_kpiValPool{nullptr};
    QLabel* m_kpiSubPool{nullptr};

    QLabel* m_kpiTitleHash{nullptr};
    QLabel* m_kpiValHash{nullptr};
    QLabel* m_kpiSubHash{nullptr};

    QLabel* m_kpiTitleSol{nullptr};
    QLabel* m_kpiValSol{nullptr};
    QLabel* m_kpiSubSol{nullptr};

    QLabel* m_kpiTitleGpu{nullptr};
    QLabel* m_kpiValGpu{nullptr};
    QLabel* m_kpiSubGpu{nullptr};

    QLabel* m_kpiTitleNet{nullptr};
    QLabel* m_kpiValNet{nullptr};
    QLabel* m_kpiSubNet{nullptr};

    // Filter toolbar
    QButtonGroup* m_statusFilterGroup{nullptr};
    QPushButton* m_pillAll{nullptr};
    QPushButton* m_pillRunning{nullptr};
    QPushButton* m_pillStopped{nullptr};
    QPushButton* m_pillWarnings{nullptr};
    QComboBox* m_comboEngineFilter{nullptr};
    QComboBox* m_comboTargetFilter{nullptr};
    QLineEdit* m_editSearch{nullptr};
    QLabel* m_lblFilterCount{nullptr};

    // Table
    QTableWidget* m_table{nullptr};

    // Inspector & Log
    QLabel* m_lblInspectorHeader{nullptr};
    QLabel* m_lblInspectorMeta{nullptr};
    QFrame* m_warnBanner{nullptr};
    QLabel* m_lblWarnBanner{nullptr};
    QPushButton* m_btnFixDoubleMine{nullptr};
    QPushButton* m_btnCopyAddr{nullptr};
    QPushButton* m_btnClearLog{nullptr};
    QCheckBox* m_chkAutoScroll{nullptr};
    QPlainTextEdit* m_logViewer{nullptr};

    // Tray
    QSystemTrayIcon* m_tray{nullptr};
    QAction* m_actTrayShow{nullptr};
    QAction* m_actTrayStartAll{nullptr};
    QAction* m_actTrayStopAll{nullptr};
    QAction* m_actTrayQuit{nullptr};
};
