#pragma once

#include "models.h"

#include <QDialog>
#include <QVector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QWidget;
class RpcClient;

/**
 * Modal dialog for creating or editing a cpuminer/ccminer worker profile.
 */
class WorkerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit WorkerDialog(const WorkerProfile* existingProfile,
                          RpcClient* rpc,
                          const QVector<PodmanContainerInfo>& pods,
                          const QVector<GpuTelemetry>& gpus,
                          QWidget* parent = nullptr);

    WorkerProfile resultProfile() const;

private Q_SLOTS:
    void onEngineChanged(int idx);
    void onTargetTypeChanged(int idx);
    void onPodmanSelectionChanged(const QString& podName);
    void onAddressChanged(const QString& text);
    void onBrowseBinary();
    void onBrowseConf();
    void onTestBinary();
    void onAcceptClicked();

private:
    void populateAddresses();
    void updateVisibilityForEngineAndTarget();
    void validateAddressWarning();
    QString extractedCoinbaseAddress() const;

    RpcClient* m_rpc{nullptr};
    QVector<PodmanContainerInfo> m_pods;
    QVector<GpuTelemetry> m_gpus;
    WorkerProfile m_original;
    bool m_isEdit{false};
    bool m_suppressBinaryAutoSwitch{false};

    QLineEdit* m_editName{nullptr};
    QLineEdit* m_editId{nullptr};
    QComboBox* m_comboEngine{nullptr};
    QFormLayout* m_form{nullptr};
    QWidget* m_rowBinary{nullptr};
    QLineEdit* m_editBinary{nullptr};
    QPushButton* m_btnBrowseBin{nullptr};
    QPushButton* m_btnTestBin{nullptr};

    QComboBox* m_comboTarget{nullptr};
    QWidget* m_rowConf{nullptr};
    QLineEdit* m_editConfPath{nullptr};
    QWidget* m_rowPodman{nullptr};
    QComboBox* m_comboPodman{nullptr};
    QWidget* m_rowRpcUrl{nullptr};
    QLineEdit* m_editRpcUrl{nullptr};
    QWidget* m_rowRpcAuth{nullptr};
    QLineEdit* m_editRpcUser{nullptr};
    QLineEdit* m_editRpcPass{nullptr};
    QWidget* m_rowSsh{nullptr};
    QLineEdit* m_editSshTarget{nullptr};
    QSpinBox* m_spinSshPort{nullptr};
    QLineEdit* m_editRemoteBin{nullptr};

    QComboBox* m_comboAddress{nullptr};
    QFrame* m_warnFrame{nullptr};
    QLabel* m_warnLabel{nullptr};

    QWidget* m_cpuSection{nullptr};
    QSpinBox* m_spinThreads{nullptr};
    QLineEdit* m_editAffinity{nullptr};
    QSpinBox* m_spinMaxTemp{nullptr};

    QWidget* m_gpuSection{nullptr};
    QComboBox* m_comboGpuDevices{nullptr};
    QDoubleSpinBox* m_spinGpuIntensity{nullptr};

    QLineEdit* m_editExtraArgs{nullptr};
    QCheckBox* m_chkAutoRestart{nullptr};
    QCheckBox* m_chkDisableInternalGen{nullptr};
};
