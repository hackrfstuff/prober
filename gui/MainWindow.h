#pragma once

#include "types.h"
#include "backend/CliRunner.h"
#include "models/EscListModel.h"

#include <QMainWindow>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QListView>
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QSettings>
#include <QSet>
#include <QProgressBar>
#include <QSlider>
#include <QStackedWidget>
#include <QGridLayout>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include "SettingsMeta.h"

namespace gui {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void onRefreshPorts();
    void onConnectAndRead();
    void onCancel();
    void onEscSelectionChanged(const QModelIndex& current, const QModelIndex& previous);

    void onUiEvent(const QJsonObject& event);
    void onLogLine(const QString& stream, const QString& text);
    void onProcessExited(int code, bool cancelled);

    void onPickFirmware();
    void onClearFirmware();
    void onStartFlash();
    void onWriteSettings();

    void onSettingChanged();

    // C2 slots
    void onC2RefreshPorts();
    void onC2Detect();
    void onC2InstallUno();
    void onC2InstallNano();
    void onC2ReadInfo();
    void onC2PickHex();
    void onC2ClearHex();
    void onC2Write();
    void onC2ModeChanged(int index);
    void onC2CancelBatch();
    void c2BatchAdvance();
    void onTabChanged(int index);

private:
    void setupUi();
    void setupConnections();
    void loadSettings();
    void saveSettings();

    void appendLog(const QString& text);
    void flushLogBuffer();
    void showWarning(const QString& text);
    void clearWarning();

    void updateUiState();
    void updateSettingsPanel();
    void updateDirtyIndicators();
    void buildSettingRows(int layoutVersion);
    void syncFriendlyFromRaw();
    void applyLayoutVisibility(int layoutVersion);
    void onFriendlyToggled(bool friendly);

    static QString normalizeTargetToSlug(const QString& target);
    QString findBundledBluejayHex(const EscIdentity& id, const QString& version);
    void maybeAutoPickBundledFirmware();

    int normalizeRwIndex(int escIndex1Based);
    int normalizeFlashIndex(int index0Based);

    void handlePassthroughInfo(const QJsonObject& ev);
    void handleMappingResolved(const QJsonObject& ev);
    void handleEscReachability(const QJsonObject& ev);
    void handleEscReadStart(const QJsonObject& ev);
    void handleEscReadOk(const QJsonObject& ev);
    void handleEscReadFail(const QJsonObject& ev);
    void handleEscReadDegraded(const QJsonObject& ev);
    void handleEscReadSummary(const QJsonObject& ev);
    void handleEscSelectFail(const QJsonObject& ev);
    void handleEscSettings(const QJsonObject& ev);
    void handleEscWriteStart(const QJsonObject& ev);
    void handleEscWriteOk(const QJsonObject& ev);
    void handleEscWriteFail(const QJsonObject& ev);
    void handleFlashPlan(const QJsonObject& ev);
    void handleEscFlashStart(const QJsonObject& ev);
    void handleEscFlashOk(const QJsonObject& ev);
    void handleEscFlashFail(const QJsonObject& ev);
    void handleEscFlashSkipped(const QJsonObject& ev);
    void handleOpDone(const QJsonObject& ev);
    void handleMspRestore(const QJsonObject& ev);
    void handleUiHello(const QJsonObject& ev);

    // C2 event handlers
    void handleC2DetectOk(const QJsonObject& ev);
    void handleC2DetectFail(const QJsonObject& ev);
    void handleC2TargetInfo(const QJsonObject& ev);
    void handleC2ReadInfoFail(const QJsonObject& ev);
    void handleC2EraseOk(const QJsonObject& ev);
    void handleC2EraseFail(const QJsonObject& ev);
    void handleC2WriteProgress(const QJsonObject& ev);
    void handleC2WriteOk(const QJsonObject& ev);
    void handleC2WriteFail(const QJsonObject& ev);
    void handleC2InstallOk(const QJsonObject& ev);
    void handleC2InstallFail(const QJsonObject& ev);
    void handleC2InstallProgress(const QJsonObject& ev);

    bool showC2PreWriteAdvice();
    void showC2PostWriteNotice();
    void showFlashPostWriteNotice();

    void doWriteNextEsc();
    void finishWriteBatch(bool success);

    void checkForUpdates();
    void onUpdateCheckFinished(QNetworkReply* reply);

    bool isCommFailure(const QString& error);

    CliRunner* cli_;
    EscListModel* escModel_;

    QComboBox* portCombo_;
    QPushButton* refreshPortsBtn_;
    QSpinBox* baudSpin_;
    QSpinBox* settleMsSpin_;
    QSpinBox* readRoundsSpin_;
    QSpinBox* readRoundSleepSpin_;
    QCheckBox* traceCheck_;
    QPushButton* connectReadBtn_;
    QPushButton* cancelBtn_;

    QListView* escListView_;
    QTabWidget* tabWidget_;

    QWidget* settingsTab_;
    QCheckBox* settingsApplyAllCheck_;
    QCheckBox* autoVerifyCheck_;
    QPushButton* writeBtn_;
    QWidget* settingsAdvancedWidget_;
    QPushButton* settingsAdvancedToggle_;
    QMap<QString, QSpinBox*> settingSpins_;
    QMap<QString, QLabel*> settingDirtyLabels_;
    QMap<QString, QStackedWidget*> settingStacks_;   // raw/friendly stacked
    QMap<QString, QWidget*> settingFriendly_;         // friendly widget (slider/combo/checkbox)
    QMap<QString, QLabel*> settingSliderValLabels_;   // value label next to sliders
    QMap<QString, QWidget*> settingRows_;             // entire row widget for visibility control
    QCheckBox* friendlyToggle_ = nullptr;
    QGridLayout* commonGrid_ = nullptr;
    QGridLayout* advGrid_ = nullptr;
    QLabel* motorDir3dWarning_ = nullptr;
    bool friendlyMode_ = true;
    int lastLayoutVersion_ = 0;

    QWidget* flashTab_;
    QComboBox* bluejayVersionCombo_ = nullptr;
    QCheckBox* useBundledCheck_ = nullptr;
    QLineEdit* firmwarePathEdit_;
    QPushButton* firmwareBrowseBtn_;
    QPushButton* firmwareClearBtn_;
    QLabel* firmwareStatusLabel_;
    QCheckBox* flashApplyAllCheck_;
    QComboBox* flashVerifyCombo_;
    QCheckBox* flashSkipMissingCheck_;
    QCheckBox* flashSlowSwitchingCheck_;
    QCheckBox* flashEraseEepromCheck_;
    QCheckBox* flashFullEraseAppCheck_;
    QCheckBox* flashFullEraseEntireAppCheck_;
    QCheckBox* flashVerifyAllBytesCheck_;
    QCheckBox* flashDryRunCheck_;
    QLineEdit* flashAssumeSigEdit_;
    QSpinBox* flashEraseRetriesSpin_;
    QSpinBox* flashWriteRetriesSpin_;
    QSpinBox* flashInterEscMsSpin_;
    QSpinBox* flashPostSelectMsSpin_;
    QSpinBox* flashVerifyReadRetriesSpin_;
    QWidget* flashAdvancedWidget_;
    QPushButton* flashAdvancedToggle_;
    QPushButton* flashBtn_;
    QLabel* flashProgressLabel_;

    // C2 Flashing tab
    QWidget* c2Tab_;
    QComboBox* c2PortCombo_;
    QPushButton* c2RefreshPortsBtn_;
    QPushButton* c2DetectBtn_;
    QLabel* c2InterfaceStatusLabel_;
    QPushButton* c2InstallUnoBtn_;
    QPushButton* c2InstallNanoBtn_;
    QPushButton* c2ReadInfoBtn_;
    QLabel* c2DeviceInfoLabel_;
    QLineEdit* c2HexPathEdit_;
    QPushButton* c2HexBrowseBtn_;
    QPushButton* c2HexClearBtn_;
    QComboBox* c2ModeCombo_;
    QLabel* c2EscCountLabel_;
    QSpinBox* c2EscCountSpin_;
    QPushButton* c2WriteBtn_;
    QPushButton* c2CancelBatchBtn_;
    QLabel* c2ProgressLabel_;
    QProgressBar* c2ProgressBar_;
    QLabel* c2WizardStatusLabel_;
    int c2WizardTotal_ = 0;
    int c2WizardCurrent_ = 0;
    bool c2WizardActive_ = false;

    bool lastFlashWasDryRun_ = false;
    int flashOkCount_ = 0;

    int savedEscSelection_ = -1;
    bool escSelectionLocked_ = false;

    QPlainTextEdit* logView_;
    QLabel* warningLabel_;

    int selectedEscIndex_ = -1;
    bool readGateOpen_ = false;
    OpKind activeOp_ = OpKind::None;

    bool uiHelloReceived_ = false;
    bool flashEventsReceived_ = false;
    QTimer* helloTimer_;

    QSet<int> flashTargets_;
    QList<int> writeQueue_;
    int writeQueueIndex_ = 0;
    bool writeBatchCancelled_ = false;

    QMap<QString, int> pendingWrites_;

    QSettings settings_;
    QNetworkAccessManager* netManager_ = nullptr;
    QLabel* updateLabel_ = nullptr;

    QStringList logBuffer_;
    QTimer* logFlushTimer_ = nullptr;
    QString cachedFwPath_;
    bool cachedFwPathValid_ = false;
};

}
