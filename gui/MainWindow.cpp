#include "MainWindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QJsonArray>
#include <QScrollArea>
#include <QFrame>
#include <QHeaderView>
#include <QStyledItemDelegate>
#include <QPainter>

namespace gui {

// Setting key lists replaced by SettingsMeta.h (commonSettingsMeta / advancedSettingsMeta)

class EscItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        painter->save();

        if (opt.state & QStyle::State_Selected) {
            painter->fillRect(opt.rect, QColor("#e0e7ff"));
        } else if (opt.state & QStyle::State_MouseOver) {
            painter->fillRect(opt.rect, QColor("#f5f5f5"));
        }

        int x = opt.rect.left() + 8;
        int y = opt.rect.top();
        int h = opt.rect.height();

        QVariant reachVar = index.data(EscListModel::ReachableRole);
        QColor dotColor("#9ca3af");
        if (reachVar.isValid()) {
            dotColor = reachVar.toBool() ? QColor("#22c55e") : QColor("#ef4444");
        }
        painter->setBrush(dotColor);
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(QPoint(x + 5, y + h / 2), 5, 5);

        x += 18;
        painter->setPen(Qt::black);
        QFont font = opt.font;
        font.setBold(true);
        painter->setFont(font);
        QString name = index.data(EscListModel::DisplayNameRole).toString();
        painter->drawText(x, y, 50, h, Qt::AlignVCenter | Qt::AlignLeft, name);

        x += 55;
        QString statusText = index.data(EscListModel::StatusTextRole).toString();
        EscStatus status = static_cast<EscStatus>(index.data(EscListModel::StatusRole).toInt());

        QColor badgeBg, badgeFg;
        switch (status) {
            case EscStatus::Ok:
                badgeBg = QColor("#dcfce7"); badgeFg = QColor("#166534"); break;
            case EscStatus::Unstable:
                badgeBg = QColor("#fef3c7"); badgeFg = QColor("#92400e"); break;
            case EscStatus::Fail:
                badgeBg = QColor("#fee2e2"); badgeFg = QColor("#991b1b"); break;
            case EscStatus::Reading:
            case EscStatus::Writing:
            case EscStatus::Flashing:
                badgeBg = QColor("#dbeafe"); badgeFg = QColor("#1e40af"); break;
            case EscStatus::Queued:
                badgeBg = QColor("#fef3c7"); badgeFg = QColor("#92400e"); break;
            default:
                badgeBg = QColor("#f3f4f6"); badgeFg = QColor("#374151"); break;
        }

        QFontMetrics fm(opt.font);
        int tw = fm.horizontalAdvance(statusText) + 12;
        QRect badgeRect(x, y + (h - 20) / 2, tw, 20);
        painter->setBrush(badgeBg);
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(badgeRect, 4, 4);
        painter->setPen(badgeFg);
        font.setBold(false);
        font.setPointSize(font.pointSize() - 1);
        painter->setFont(font);
        painter->drawText(badgeRect, Qt::AlignCenter, statusText);

        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        Q_UNUSED(option);
        Q_UNUSED(index);
        return QSize(180, 36);
    }
};

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , cli_(new CliRunner(this))
    , escModel_(new EscListModel(this))
    , settings_("prober", "GUI")
{
    setWindowTitle("prober - developed by @leszczzy");
    resize(1200, 800);

    helloTimer_ = new QTimer(this);
    helloTimer_->setSingleShot(true);
    connect(helloTimer_, &QTimer::timeout, this, [this]() {
        if (!uiHelloReceived_ && activeOp_ != OpKind::None) {
            showWarning("No UI events received from CLI. Check CLI version supports --ui-json.");
        }
    });

    setupUi();
    setupConnections();
    loadSettings();
    onRefreshPorts();
    updateUiState();
}

MainWindow::~MainWindow() {
    saveSettings();
}

void MainWindow::setupUi() {
    QWidget* central = new QWidget(this);
    setCentralWidget(central);

    QVBoxLayout* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    QWidget* topBar = new QWidget();
    topBar->setStyleSheet("background-color: #f4f4f5; border-bottom: 1px solid #e4e4e7;");
    QHBoxLayout* topBarLayout = new QHBoxLayout(topBar);
    topBarLayout->setContentsMargins(12, 8, 12, 8);
    topBarLayout->addWidget(new QLabel("<b>prober</b>"));
    topBarLayout->addStretch();
    traceCheck_ = new QCheckBox("Trace");
    autoVerifyCheck_ = new QCheckBox("Auto-verify");
    autoVerifyCheck_->setChecked(false);
    topBarLayout->addWidget(traceCheck_);
    topBarLayout->addWidget(autoVerifyCheck_);
    rootLayout->addWidget(topBar);

    QHBoxLayout* mainLayout = new QHBoxLayout();
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    rootLayout->addLayout(mainLayout, 1);

    QSplitter* splitter = new QSplitter(Qt::Horizontal);
    mainLayout->addWidget(splitter);

    QWidget* leftPanel = new QWidget();
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(12, 12, 12, 12);

    QGroupBox* connGroup = new QGroupBox("Connection");
    QGridLayout* connGrid = new QGridLayout(connGroup);

    connGrid->addWidget(new QLabel("Port:"), 0, 0);
    portCombo_ = new QComboBox();
    portCombo_->setMinimumWidth(120);
    connGrid->addWidget(portCombo_, 0, 1);
    refreshPortsBtn_ = new QPushButton("Refresh");
    connGrid->addWidget(refreshPortsBtn_, 0, 2);

    connGrid->addWidget(new QLabel("Baud:"), 1, 0);
    baudSpin_ = new QSpinBox();
    baudSpin_->setRange(9600, 921600);
    baudSpin_->setValue(115200);
    connGrid->addWidget(baudSpin_, 1, 1, 1, 2);

    connGrid->addWidget(new QLabel("Settle ms:"), 2, 0);
    settleMsSpin_ = new QSpinBox();
    settleMsSpin_->setRange(0, 5000);
    settleMsSpin_->setValue(0);
    connGrid->addWidget(settleMsSpin_, 2, 1, 1, 2);

    connGrid->addWidget(new QLabel("Read rounds:"), 3, 0);
    readRoundsSpin_ = new QSpinBox();
    readRoundsSpin_->setRange(1, 10);
    readRoundsSpin_->setValue(3);
    connGrid->addWidget(readRoundsSpin_, 3, 1, 1, 2);

    connGrid->addWidget(new QLabel("Round sleep ms:"), 4, 0);
    readRoundSleepSpin_ = new QSpinBox();
    readRoundSleepSpin_->setRange(0, 2000);
    readRoundSleepSpin_->setValue(150);
    connGrid->addWidget(readRoundSleepSpin_, 4, 1, 1, 2);

    leftLayout->addWidget(connGroup);

    QHBoxLayout* btnRow = new QHBoxLayout();
    connectReadBtn_ = new QPushButton("Connect && Read");
    connectReadBtn_->setStyleSheet("QPushButton { background-color: #18181b; color: white; padding: 8px 16px; border-radius: 4px; } QPushButton:hover { background-color: #27272a; } QPushButton:disabled { opacity: 0.5; }");
    cancelBtn_ = new QPushButton("Cancel");
    cancelBtn_->setStyleSheet("QPushButton { padding: 8px 16px; border: 1px solid #d4d4d8; border-radius: 4px; } QPushButton:hover { background-color: #f4f4f5; }");
    btnRow->addWidget(connectReadBtn_);
    btnRow->addWidget(cancelBtn_);
    leftLayout->addLayout(btnRow);

    QGroupBox* escGroup = new QGroupBox("ESCs");
    QVBoxLayout* escLayout = new QVBoxLayout(escGroup);
    escListView_ = new QListView();
    escListView_->setModel(escModel_);
    escListView_->setItemDelegate(new EscItemDelegate(escListView_));
    escListView_->setSelectionMode(QAbstractItemView::SingleSelection);
    escListView_->setStyleSheet("QListView { border: 1px solid #e4e4e7; border-radius: 4px; background: white; }");
    escLayout->addWidget(escListView_);
    leftLayout->addWidget(escGroup, 1);

    splitter->addWidget(leftPanel);

    QWidget* centerPanel = new QWidget();
    QVBoxLayout* centerLayout = new QVBoxLayout(centerPanel);
    centerLayout->setContentsMargins(0, 12, 0, 12);

    tabWidget_ = new QTabWidget();
    tabWidget_->setStyleSheet("QTabWidget::pane { border: 1px solid #e4e4e7; background: white; } QTabBar::tab { padding: 8px 16px; } QTabBar::tab:selected { background: white; border-bottom: 2px solid #18181b; }");

    settingsTab_ = new QWidget();
    QVBoxLayout* settingsLayout = new QVBoxLayout(settingsTab_);
    settingsLayout->setContentsMargins(16, 16, 16, 16);

    // Display mode toggle
    QHBoxLayout* toggleRow = new QHBoxLayout();
    friendlyToggle_ = new QCheckBox("Friendly display");
    friendlyToggle_->setChecked(true);
    friendlyMode_ = true;
    toggleRow->addWidget(friendlyToggle_);
    toggleRow->addStretch();
    settingsLayout->addLayout(toggleRow);

    QGroupBox* targetGroup = new QGroupBox("Target");
    QVBoxLayout* targetLayout = new QVBoxLayout(targetGroup);
    settingsApplyAllCheck_ = new QCheckBox("Apply to all ESCs");
    targetLayout->addWidget(settingsApplyAllCheck_);
    settingsLayout->addWidget(targetGroup);

    QGroupBox* commonGroup = new QGroupBox("Common Settings");
    commonGrid_ = new QGridLayout(commonGroup);
    settingsLayout->addWidget(commonGroup);

    settingsAdvancedToggle_ = new QPushButton("Advanced (click to expand)");
    settingsAdvancedToggle_->setFlat(true);
    settingsAdvancedToggle_->setStyleSheet("text-align: left; padding: 4px;");
    settingsLayout->addWidget(settingsAdvancedToggle_);

    settingsAdvancedWidget_ = new QWidget();
    advGrid_ = new QGridLayout(settingsAdvancedWidget_);
    settingsAdvancedWidget_->setVisible(false);
    settingsLayout->addWidget(settingsAdvancedWidget_);

    connect(settingsAdvancedToggle_, &QPushButton::clicked, this, [this]() {
        bool vis = !settingsAdvancedWidget_->isVisible();
        settingsAdvancedWidget_->setVisible(vis);
        settingsAdvancedToggle_->setText(vis ? "Advanced (click to collapse)" : "Advanced (click to expand)");
    });

    // Build initial setting rows with default layout version
    buildSettingRows(200);

    connect(friendlyToggle_, &QCheckBox::toggled, this, &MainWindow::onFriendlyToggled);

    QHBoxLayout* writeRow = new QHBoxLayout();
    writeRow->addStretch();
    writeBtn_ = new QPushButton("Write changes");
    writeBtn_->setStyleSheet("QPushButton { background-color: #18181b; color: white; padding: 8px 16px; border-radius: 4px; } QPushButton:hover { background-color: #27272a; } QPushButton:disabled { opacity: 0.5; }");
    writeRow->addWidget(writeBtn_);
    settingsLayout->addLayout(writeRow);
    settingsLayout->addStretch();

    tabWidget_->addTab(settingsTab_, "Settings");

    flashTab_ = new QWidget();
    QVBoxLayout* flashLayout = new QVBoxLayout(flashTab_);
    flashLayout->setContentsMargins(16, 16, 16, 16);

    QGroupBox* fwGroup = new QGroupBox("Firmware");
    QVBoxLayout* fwLayout = new QVBoxLayout(fwGroup);

    // Bundled Bluejay controls
    useBundledCheck_ = new QCheckBox("Use bundled Bluejay firmware (auto)");
    useBundledCheck_->setChecked(true);
    fwLayout->addWidget(useBundledCheck_);

    QHBoxLayout* verRow = new QHBoxLayout();
    verRow->addWidget(new QLabel("Bluejay version:"));
    bluejayVersionCombo_ = new QComboBox();
    bluejayVersionCombo_->addItem("0.21.0", "0.21.0");
    bluejayVersionCombo_->addItem("0.19.2", "0.19.2");
    verRow->addWidget(bluejayVersionCombo_);
    verRow->addStretch();
    fwLayout->addLayout(verRow);

    QHBoxLayout* fwRow = new QHBoxLayout();
    firmwarePathEdit_ = new QLineEdit();
    firmwarePathEdit_->setReadOnly(true);
    firmwarePathEdit_->setPlaceholderText("No firmware selected");
    firmwareBrowseBtn_ = new QPushButton("Browse...");
    firmwareClearBtn_ = new QPushButton("Clear");
    fwRow->addWidget(firmwarePathEdit_, 1);
    fwRow->addWidget(firmwareBrowseBtn_);
    fwRow->addWidget(firmwareClearBtn_);
    fwLayout->addLayout(fwRow);
    firmwareStatusLabel_ = new QLabel();
    fwLayout->addWidget(firmwareStatusLabel_);
    flashLayout->addWidget(fwGroup);

    QGroupBox* flashTargetGroup = new QGroupBox("Target");
    QVBoxLayout* flashTargetLayout = new QVBoxLayout(flashTargetGroup);
    flashApplyAllCheck_ = new QCheckBox("Apply to all ESCs");
    flashTargetLayout->addWidget(flashApplyAllCheck_);
    flashLayout->addWidget(flashTargetGroup);

    QGroupBox* verifyGroup = new QGroupBox("Verify");
    QVBoxLayout* verifyLayout = new QVBoxLayout(verifyGroup);
    flashVerifyCombo_ = new QComboBox();
    flashVerifyCombo_->addItem("off", "off");
    flashVerifyCombo_->addItem("fast", "fast");
    flashVerifyCombo_->addItem("full", "full");
    verifyLayout->addWidget(flashVerifyCombo_);
    flashLayout->addWidget(verifyGroup);

    flashAdvancedToggle_ = new QPushButton("Advanced (click to expand)");
    flashAdvancedToggle_->setFlat(true);
    flashAdvancedToggle_->setStyleSheet("text-align: left; padding: 4px;");
    flashLayout->addWidget(flashAdvancedToggle_);

    flashAdvancedWidget_ = new QWidget();
    QVBoxLayout* flashAdvLayout = new QVBoxLayout(flashAdvancedWidget_);
    flashSkipMissingCheck_ = new QCheckBox("Skip unreachable ESCs (recommended)");
    flashSkipMissingCheck_->setChecked(true);
    flashSlowSwitchingCheck_ = new QCheckBox("Slow ESC switching (recommended for some AIOs)");
    flashEraseEepromCheck_ = new QCheckBox("Erase EEPROM");
    flashEraseEepromCheck_->setStyleSheet("color: #b45309;");
    flashFullEraseAppCheck_ = new QCheckBox("Full erase app");
    flashFullEraseEntireAppCheck_ = new QCheckBox("Full erase entire app");
    flashVerifyAllBytesCheck_ = new QCheckBox("Verify all bytes");
    flashDryRunCheck_ = new QCheckBox("Dry run");
    flashAdvLayout->addWidget(flashSkipMissingCheck_);
    flashAdvLayout->addWidget(flashSlowSwitchingCheck_);
    flashAdvLayout->addWidget(flashEraseEepromCheck_);
    flashAdvLayout->addWidget(flashFullEraseAppCheck_);
    flashAdvLayout->addWidget(flashFullEraseEntireAppCheck_);
    flashAdvLayout->addWidget(flashVerifyAllBytesCheck_);
    flashAdvLayout->addWidget(flashDryRunCheck_);
    QHBoxLayout* sigRow = new QHBoxLayout();
    sigRow->addWidget(new QLabel("Assume signature:"));
    flashAssumeSigEdit_ = new QLineEdit();
    flashAssumeSigEdit_->setPlaceholderText("0xE8B5");
    sigRow->addWidget(flashAssumeSigEdit_);
    flashAdvLayout->addLayout(sigRow);

    // Stability controls
    flashAdvLayout->addWidget(new QLabel("Stability tuning:"));
    auto addSpinRow = [&](const QString& label, QSpinBox*& spin, int min, int max, int def, const QString& suffix) {
        QHBoxLayout* row = new QHBoxLayout();
        row->addWidget(new QLabel(label));
        spin = new QSpinBox();
        spin->setRange(min, max);
        spin->setValue(def);
        if (!suffix.isEmpty()) spin->setSuffix(suffix);
        row->addWidget(spin);
        flashAdvLayout->addLayout(row);
    };
    addSpinRow("Erase retries:", flashEraseRetriesSpin_, 1, 10, 3, "");
    addSpinRow("Write retries:", flashWriteRetriesSpin_, 1, 10, 3, "");
    addSpinRow("Inter-ESC delay:", flashInterEscMsSpin_, 0, 3000, 250, " ms");
    addSpinRow("Post-select settle:", flashPostSelectMsSpin_, 0, 2000, 200, " ms");
    addSpinRow("Verify read retries:", flashVerifyReadRetriesSpin_, 1, 10, 3, "");

    flashAdvancedWidget_->setVisible(false);
    flashLayout->addWidget(flashAdvancedWidget_);

    connect(flashAdvancedToggle_, &QPushButton::clicked, this, [this]() {
        bool vis = !flashAdvancedWidget_->isVisible();
        flashAdvancedWidget_->setVisible(vis);
        flashAdvancedToggle_->setText(vis ? "Advanced (click to collapse)" : "Advanced (click to expand)");
    });

    QHBoxLayout* flashBtnRow = new QHBoxLayout();
    flashProgressLabel_ = new QLabel();
    flashBtnRow->addWidget(flashProgressLabel_);
    flashBtnRow->addStretch();
    flashBtn_ = new QPushButton("Flash");
    flashBtn_->setStyleSheet("QPushButton { background-color: #18181b; color: white; padding: 8px 16px; border-radius: 4px; } QPushButton:hover { background-color: #27272a; } QPushButton:disabled { opacity: 0.5; }");
    flashBtnRow->addWidget(flashBtn_);
    flashLayout->addLayout(flashBtnRow);
    flashLayout->addStretch();

    tabWidget_->addTab(flashTab_, "Flashing");

    // ========== C2 Flashing Tab ==========
    c2Tab_ = new QWidget();
    QVBoxLayout* c2Layout = new QVBoxLayout(c2Tab_);
    c2Layout->setContentsMargins(16, 16, 16, 16);

    QGroupBox* c2ConnGroup = new QGroupBox("Arduino Connection");
    QVBoxLayout* c2ConnLayout = new QVBoxLayout(c2ConnGroup);
    QHBoxLayout* c2PortRow = new QHBoxLayout();
    c2PortRow->addWidget(new QLabel("Port:"));
    c2PortCombo_ = new QComboBox();
    c2PortCombo_->setMinimumWidth(120);
    c2PortRow->addWidget(c2PortCombo_, 1);
    c2RefreshPortsBtn_ = new QPushButton("Refresh");
    c2PortRow->addWidget(c2RefreshPortsBtn_);
    c2ConnLayout->addLayout(c2PortRow);

    QHBoxLayout* c2DetectRow = new QHBoxLayout();
    c2DetectBtn_ = new QPushButton("Detect Interface");
    c2DetectRow->addWidget(c2DetectBtn_);
    c2InterfaceStatusLabel_ = new QLabel();
    c2DetectRow->addWidget(c2InterfaceStatusLabel_, 1);
    c2ConnLayout->addLayout(c2DetectRow);
    c2Layout->addWidget(c2ConnGroup);

    QGroupBox* c2InstallGroup = new QGroupBox("Install Interface Firmware");
    QHBoxLayout* c2InstallLayout = new QHBoxLayout(c2InstallGroup);
    c2InstallUnoBtn_ = new QPushButton("Install UNO");
    c2InstallNanoBtn_ = new QPushButton("Install Nano");
    c2InstallLayout->addWidget(c2InstallUnoBtn_);
    c2InstallLayout->addWidget(c2InstallNanoBtn_);
    c2InstallLayout->addStretch();
    c2Layout->addWidget(c2InstallGroup);

    QGroupBox* c2InfoGroup = new QGroupBox("Target Info");
    QVBoxLayout* c2InfoLayout = new QVBoxLayout(c2InfoGroup);
    QHBoxLayout* c2InfoRow = new QHBoxLayout();
    c2ReadInfoBtn_ = new QPushButton("Read Target Info");
    c2InfoRow->addWidget(c2ReadInfoBtn_);
    c2DeviceInfoLabel_ = new QLabel("Not connected");
    c2DeviceInfoLabel_->setStyleSheet("color: #71717a;");
    c2InfoRow->addWidget(c2DeviceInfoLabel_, 1);
    c2InfoLayout->addLayout(c2InfoRow);
    c2Layout->addWidget(c2InfoGroup);

    QGroupBox* c2WriteGroup = new QGroupBox("Write Target");
    QVBoxLayout* c2WriteLayout = new QVBoxLayout(c2WriteGroup);
    QHBoxLayout* c2HexRow = new QHBoxLayout();
    c2HexPathEdit_ = new QLineEdit();
    c2HexPathEdit_->setReadOnly(true);
    c2HexPathEdit_->setPlaceholderText("No firmware selected");
    c2HexBrowseBtn_ = new QPushButton("Browse...");
    c2HexClearBtn_ = new QPushButton("Clear");
    c2HexRow->addWidget(c2HexPathEdit_, 1);
    c2HexRow->addWidget(c2HexBrowseBtn_);
    c2HexRow->addWidget(c2HexClearBtn_);
    c2WriteLayout->addLayout(c2HexRow);

    QHBoxLayout* c2ModeRow = new QHBoxLayout();
    c2ModeCombo_ = new QComboBox();
    c2ModeCombo_->addItem("Single", "single");
    c2ModeCombo_->addItem("Batch", "batch");
    c2ModeRow->addWidget(new QLabel("Mode:"));
    c2ModeRow->addWidget(c2ModeCombo_);
    c2ModeRow->addSpacing(16);
    c2EscCountLabel_ = new QLabel("ESCs:");
    c2EscCountSpin_ = new QSpinBox();
    c2EscCountSpin_->setRange(1, 16);
    c2EscCountSpin_->setValue(4);
    c2EscCountLabel_->setVisible(false);
    c2EscCountSpin_->setVisible(false);
    c2ModeRow->addWidget(c2EscCountLabel_);
    c2ModeRow->addWidget(c2EscCountSpin_);
    c2ModeRow->addStretch();
    c2WriteLayout->addLayout(c2ModeRow);

    c2ProgressBar_ = new QProgressBar();
    c2ProgressBar_->setVisible(false);
    c2WriteLayout->addWidget(c2ProgressBar_);

    // Status label for batch mode progress
    c2WizardStatusLabel_ = new QLabel();
    c2WizardStatusLabel_->setWordWrap(true);
    c2WizardStatusLabel_->setVisible(false);
    c2WriteLayout->addWidget(c2WizardStatusLabel_);

    QHBoxLayout* c2WriteBtnRow = new QHBoxLayout();
    c2ProgressLabel_ = new QLabel();
    c2WriteBtnRow->addWidget(c2ProgressLabel_);
    c2WriteBtnRow->addStretch();
    c2CancelBatchBtn_ = new QPushButton("Cancel");
    c2CancelBatchBtn_->setVisible(false);
    c2WriteBtnRow->addWidget(c2CancelBatchBtn_);
    c2WriteBtn_ = new QPushButton("Erase + Write");
    c2WriteBtn_->setStyleSheet("QPushButton { background-color: #18181b; color: white; padding: 8px 16px; border-radius: 4px; } QPushButton:hover { background-color: #27272a; } QPushButton:disabled { opacity: 0.5; }");
    c2WriteBtnRow->addWidget(c2WriteBtn_);
    c2WriteLayout->addLayout(c2WriteBtnRow);
    c2Layout->addWidget(c2WriteGroup);

    c2Layout->addStretch();
    tabWidget_->addTab(c2Tab_, "C2 Flashing");

    centerLayout->addWidget(tabWidget_);
    splitter->addWidget(centerPanel);

    QWidget* rightPanel = new QWidget();
    QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(12, 12, 12, 12);

    QHBoxLayout* logHeader = new QHBoxLayout();
    logHeader->addWidget(new QLabel("Logs"));
    logHeader->addStretch();
    QPushButton* clearLogBtn = new QPushButton("Clear");
    connect(clearLogBtn, &QPushButton::clicked, this, [this]() { logView_->clear(); });
    logHeader->addWidget(clearLogBtn);
    rightLayout->addLayout(logHeader);

    warningLabel_ = new QLabel();
    warningLabel_->setStyleSheet("background-color: #fef3c7; color: #92400e; padding: 8px; border-radius: 4px;");
    warningLabel_->setWordWrap(true);
    warningLabel_->setVisible(false);
    rightLayout->addWidget(warningLabel_);

    logView_ = new QPlainTextEdit();
    logView_->setReadOnly(true);
    logView_->setStyleSheet("QPlainTextEdit { background-color: #18181b; color: #fafafa; font-family: monospace; font-size: 11px; border-radius: 4px; }");
    logView_->setMaximumBlockCount(1000);
    rightLayout->addWidget(logView_, 1);

    splitter->addWidget(rightPanel);
    splitter->setSizes({280, 500, 350});

    setStyleSheet("QMainWindow { background-color: #fafafa; } QGroupBox { font-weight: bold; border: 1px solid #e4e4e7; border-radius: 4px; margin-top: 8px; padding-top: 8px; } QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }");
}

void MainWindow::setupConnections() {
    connect(refreshPortsBtn_, &QPushButton::clicked, this, &MainWindow::onRefreshPorts);
    connect(connectReadBtn_, &QPushButton::clicked, this, &MainWindow::onConnectAndRead);
    connect(cancelBtn_, &QPushButton::clicked, this, &MainWindow::onCancel);

    connect(escListView_->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &MainWindow::onEscSelectionChanged);

    connect(cli_, &CliRunner::uiEvent, this, &MainWindow::onUiEvent);
    connect(cli_, &CliRunner::logLine, this, &MainWindow::onLogLine);
    connect(cli_, &CliRunner::processExited, this, &MainWindow::onProcessExited);

    connect(firmwareBrowseBtn_, &QPushButton::clicked, this, &MainWindow::onPickFirmware);
    connect(firmwareClearBtn_, &QPushButton::clicked, this, &MainWindow::onClearFirmware);
    connect(flashBtn_, &QPushButton::clicked, this, &MainWindow::onStartFlash);
    connect(writeBtn_, &QPushButton::clicked, this, &MainWindow::onWriteSettings);

    // Bundled Bluejay firmware controls
    connect(useBundledCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        firmwareBrowseBtn_->setEnabled(!checked);
        firmwareClearBtn_->setEnabled(!checked);
        bluejayVersionCombo_->setEnabled(checked);
        if (checked) {
            maybeAutoPickBundledFirmware();
        }
    });
    connect(bluejayVersionCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (useBundledCheck_->isChecked()) {
            maybeAutoPickBundledFirmware();
        }
    });
    connect(flashApplyAllCheck_, &QCheckBox::toggled, this, [this](bool) {
        if (useBundledCheck_->isChecked()) {
            maybeAutoPickBundledFirmware();
        }
    });

    // C2 connections
    connect(c2RefreshPortsBtn_, &QPushButton::clicked, this, &MainWindow::onC2RefreshPorts);
    connect(c2DetectBtn_, &QPushButton::clicked, this, &MainWindow::onC2Detect);
    connect(c2InstallUnoBtn_, &QPushButton::clicked, this, &MainWindow::onC2InstallUno);
    connect(c2InstallNanoBtn_, &QPushButton::clicked, this, &MainWindow::onC2InstallNano);
    connect(c2ReadInfoBtn_, &QPushButton::clicked, this, &MainWindow::onC2ReadInfo);
    connect(c2HexBrowseBtn_, &QPushButton::clicked, this, &MainWindow::onC2PickHex);
    connect(c2HexClearBtn_, &QPushButton::clicked, this, &MainWindow::onC2ClearHex);
    connect(c2WriteBtn_, &QPushButton::clicked, this, &MainWindow::onC2Write);
    connect(c2ModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onC2ModeChanged);
    connect(c2CancelBatchBtn_, &QPushButton::clicked, this, &MainWindow::onC2CancelBatch);

    connect(tabWidget_, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
}

void MainWindow::loadSettings() {
    baudSpin_->setValue(settings_.value("baud", 115200).toInt());
    settleMsSpin_->setValue(settings_.value("settleMs", 0).toInt());
    readRoundsSpin_->setValue(settings_.value("readRounds", 3).toInt());
    readRoundSleepSpin_->setValue(settings_.value("readRoundSleepMs", 150).toInt());
    traceCheck_->setChecked(settings_.value("trace", false).toBool());
    autoVerifyCheck_->setChecked(settings_.value("autoVerify", false).toBool());

    // Bundled Bluejay firmware settings
    useBundledCheck_->setChecked(settings_.value("useBundledBluejay", true).toBool());
    QString savedVer = settings_.value("bluejayVersion", "0.21.0").toString();
    int verIdx = bluejayVersionCombo_->findData(savedVer);
    if (verIdx >= 0) bluejayVersionCombo_->setCurrentIndex(verIdx);

    // If bundled mode, auto-pick will be called after ESC read; for now set manual path
    if (!useBundledCheck_->isChecked()) {
        firmwarePathEdit_->setText(settings_.value("firmwarePath", "").toString());
        QString fwPath = firmwarePathEdit_->text();
        if (!fwPath.isEmpty() && QFileInfo::exists(fwPath)) {
            firmwareStatusLabel_->setText(QString::fromUtf8("\u2713 File exists"));
            firmwareStatusLabel_->setStyleSheet("color: #16a34a;");
        } else if (!fwPath.isEmpty()) {
            firmwareStatusLabel_->setText(QString::fromUtf8("\u2717 File not found"));
            firmwareStatusLabel_->setStyleSheet("color: #dc2626;");
        }
    }
    // Apply bundled mode UI state
    firmwareBrowseBtn_->setEnabled(!useBundledCheck_->isChecked());
    firmwareClearBtn_->setEnabled(!useBundledCheck_->isChecked());
    bluejayVersionCombo_->setEnabled(useBundledCheck_->isChecked());

    // C2 settings
    c2HexPathEdit_->setText(settings_.value("c2HexPath", "").toString());
}

void MainWindow::saveSettings() {
    settings_.setValue("baud", baudSpin_->value());
    settings_.setValue("settleMs", settleMsSpin_->value());
    settings_.setValue("readRounds", readRoundsSpin_->value());
    settings_.setValue("readRoundSleepMs", readRoundSleepSpin_->value());
    settings_.setValue("trace", traceCheck_->isChecked());
    settings_.setValue("autoVerify", autoVerifyCheck_->isChecked());

    // Bundled Bluejay firmware settings
    settings_.setValue("useBundledBluejay", useBundledCheck_->isChecked());
    settings_.setValue("bluejayVersion", bluejayVersionCombo_->currentData().toString());
    // Only persist manual firmware path if not using bundled
    if (!useBundledCheck_->isChecked()) {
        settings_.setValue("firmwarePath", firmwarePathEdit_->text());
    }
    
    // C2 settings
    settings_.setValue("c2HexPath", c2HexPathEdit_->text());
}

void MainWindow::appendLog(const QString& text) {
    logView_->appendPlainText(text);
}

void MainWindow::showWarning(const QString& text) {
    warningLabel_->setText(text);
    warningLabel_->setVisible(true);
}

void MainWindow::clearWarning() {
    warningLabel_->setVisible(false);
}

void MainWindow::updateUiState() {
    bool busy = cli_->isBusy() || !writeQueue_.isEmpty();
    connectReadBtn_->setEnabled(!busy && !portCombo_->currentText().isEmpty());
    cancelBtn_->setEnabled(busy);

    bool canWrite = readGateOpen_ && !busy && selectedEscIndex_ >= 0;
    if (settingsApplyAllCheck_->isChecked()) {
        canWrite = readGateOpen_ && !busy && !escModel_->indicesWithBaseSettings().isEmpty();
    }
    bool hasDirty = !pendingWrites_.isEmpty();
    writeBtn_->setEnabled(canWrite && hasDirty);

    QString fwPath = firmwarePathEdit_->text();
    bool fwExists = !fwPath.isEmpty() && QFileInfo::exists(fwPath);
    bool canFlash = readGateOpen_ && !busy && fwExists;
    if (flashApplyAllCheck_->isChecked()) {
        canFlash = canFlash && !escModel_->indicesWithBaseSettings().isEmpty();
    } else {
        canFlash = canFlash && selectedEscIndex_ >= 0;
    }
    flashBtn_->setEnabled(canFlash);

    for (auto it = settingSpins_.begin(); it != settingSpins_.end(); ++it) {
        it.value()->setEnabled(!busy && selectedEscIndex_ >= 0);
    }
}

void MainWindow::updateSettingsPanel() {
    if (selectedEscIndex_ < 0) {
        for (auto spin : settingSpins_) {
            spin->blockSignals(true);
            spin->setValue(0);
            spin->blockSignals(false);
        }
        for (auto lbl : settingDirtyLabels_) {
            lbl->clear();
        }
        return;
    }

    EscState esc = escModel_->escAt(selectedEscIndex_);

    // Rebuild rows if layout version changed
    int lv = esc.identity ? esc.identity->layoutVersion : 200;
    if (lv != lastLayoutVersion_) {
        buildSettingRows(lv);
    }

    for (auto it = settingSpins_.begin(); it != settingSpins_.end(); ++it) {
        QString key = it.key();
        QSpinBox* spin = it.value();
        spin->blockSignals(true);
        if (esc.settings.contains(key)) {
            spin->setValue(esc.settings.value(key));
        } else {
            spin->setValue(0);
        }
        spin->blockSignals(false);
    }
    syncFriendlyFromRaw();
    applyLayoutVisibility(lv);
    updateDirtyIndicators();
}

void MainWindow::updateDirtyIndicators() {
    pendingWrites_.clear();
    if (selectedEscIndex_ < 0) {
        for (auto lbl : settingDirtyLabels_) lbl->clear();
        return;
    }

    EscState esc = escModel_->escAt(selectedEscIndex_);
    for (auto it = settingSpins_.begin(); it != settingSpins_.end(); ++it) {
        QString key = it.key();
        QSpinBox* spin = it.value();
        QLabel* lbl = settingDirtyLabels_.value(key);
        int curVal = spin->value();
        int baseVal = esc.baseSettings.value(key, curVal);
        if (curVal != baseVal) {
            lbl->setText("*");
            pendingWrites_[key] = curVal;
        } else {
            lbl->clear();
        }
    }
    updateUiState();
}

// ========== Friendly settings UI ==========

void MainWindow::buildSettingRows(int layoutVersion) {
    lastLayoutVersion_ = layoutVersion;

    // Clear existing widgets from grids
    auto clearGrid = [](QGridLayout* grid) {
        while (grid->count() > 0) {
            QLayoutItem* item = grid->takeAt(0);
            if (item->widget()) item->widget()->deleteLater();
            delete item;
        }
    };
    clearGrid(commonGrid_);
    clearGrid(advGrid_);
    settingSpins_.clear();
    settingDirtyLabels_.clear();
    settingStacks_.clear();
    settingFriendly_.clear();
    settingSliderValLabels_.clear();
    settingRows_.clear();
    if (motorDir3dWarning_) { motorDir3dWarning_->deleteLater(); motorDir3dWarning_ = nullptr; }

    auto allMeta = commonSettingsMeta(layoutVersion);
    allMeta.append(advancedSettingsMeta(layoutVersion));

    for (const auto& meta : allMeta) {
        QGridLayout* grid = meta.isCommon ? commonGrid_ : advGrid_;
        int row = grid->rowCount();

        // Label
        QLabel* lbl = new QLabel(meta.label + ":");

        // Raw spinbox (canonical value store)
        QSpinBox* spin = new QSpinBox();
        spin->setRange(meta.rawMin, meta.rawMax);
        spin->setSingleStep(meta.step);
        spin->setObjectName(meta.key);

        // Dirty indicator
        QLabel* dirtyLbl = new QLabel();
        dirtyLbl->setStyleSheet("color: #f59e0b; font-weight: bold;");

        // Build friendly widget
        QWidget* friendlyWidget = nullptr;

        if (meta.type == FriendlyType::Dropdown) {
            QComboBox* combo = new QComboBox();
            for (const auto& ev : meta.enumLabels) {
                combo->addItem(ev.second, ev.first);
            }
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, combo, spin](int) {
                int rawVal = combo->currentData().toInt();
                spin->blockSignals(true);
                spin->setValue(rawVal);
                spin->blockSignals(false);
                updateDirtyIndicators();
            });
            friendlyWidget = combo;
        } else if (meta.type == FriendlyType::Slider) {
            QWidget* sliderContainer = new QWidget();
            QHBoxLayout* sliderLayout = new QHBoxLayout(sliderContainer);
            sliderLayout->setContentsMargins(0, 0, 0, 0);
            int steps = (meta.rawMax - meta.rawMin) / meta.step;
            QSlider* slider = new QSlider(Qt::Horizontal);
            slider->setRange(0, steps);
            QLabel* valLabel = new QLabel();
            valLabel->setMinimumWidth(40);
            valLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            sliderLayout->addWidget(slider, 1);
            sliderLayout->addWidget(valLabel);
            settingSliderValLabels_[meta.key] = valLabel;

            int sMin = meta.rawMin, sStep = meta.step;
            connect(slider, &QSlider::valueChanged, this, [this, slider, spin, valLabel, sMin, sStep](int pos) {
                int rawVal = sMin + pos * sStep;
                valLabel->setText(QString::number(rawVal));
                spin->blockSignals(true);
                spin->setValue(rawVal);
                spin->blockSignals(false);
                updateDirtyIndicators();
            });
            friendlyWidget = sliderContainer;
        } else if (meta.type == FriendlyType::Checkbox) {
            QCheckBox* cb = new QCheckBox();
            connect(cb, &QCheckBox::toggled, this, [this, cb, spin](bool checked) {
                spin->blockSignals(true);
                spin->setValue(checked ? 1 : 0);
                spin->blockSignals(false);
                updateDirtyIndicators();
            });
            friendlyWidget = cb;
        }

        QStackedWidget* stack = new QStackedWidget();
        if (friendlyWidget) {
            stack->addWidget(friendlyWidget);
        } else {
            stack->addWidget(new QWidget()); // placeholder
        }
        stack->addWidget(spin);
        stack->setCurrentIndex(friendlyMode_ ? 0 : 1);

        grid->addWidget(lbl, row, 0);
        grid->addWidget(stack, row, 1);
        grid->addWidget(dirtyLbl, row, 2);

        settingSpins_[meta.key] = spin;
        settingDirtyLabels_[meta.key] = dirtyLbl;
        settingStacks_[meta.key] = stack;
        if (friendlyWidget) settingFriendly_[meta.key] = friendlyWidget;

        // Connect raw spinbox change (for raw mode editing)
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onSettingChanged);

        if (meta.key == "MOTOR_DIRECTION") {
            motorDir3dWarning_ = new QLabel("3D mode value detected. Switch to Raw mode to edit.");
            motorDir3dWarning_->setStyleSheet("color: #b45309; font-size: 11px;");
            motorDir3dWarning_->setVisible(false);
            grid->addWidget(motorDir3dWarning_, row + 1, 0, 1, 3);
        }
    }
}

void MainWindow::syncFriendlyFromRaw() {
    for (auto it = settingSpins_.begin(); it != settingSpins_.end(); ++it) {
        QString key = it.key();
        QSpinBox* spin = it.value();
        int rawVal = spin->value();

        QWidget* fw = settingFriendly_.value(key, nullptr);
        if (!fw) continue;

        // Dropdown
        if (auto combo = qobject_cast<QComboBox*>(fw)) {
            combo->blockSignals(true);
            int idx = combo->findData(rawVal);
            if (idx >= 0) {
                combo->setCurrentIndex(idx);
                combo->setEnabled(true);
            } else {
                // Value not in enum (e.g. MOTOR_DIRECTION 3/4)
                combo->setCurrentIndex(-1);
                if (key == "MOTOR_DIRECTION" && motorDir3dWarning_) {
                    motorDir3dWarning_->setVisible(friendlyMode_ && (rawVal == 3 || rawVal == 4));
                    combo->setEnabled(false);
                }
            }
            combo->blockSignals(false);
            continue;
        }

        // Slider container — find the QSlider inside
        if (fw->layout()) {
            QSlider* slider = fw->findChild<QSlider*>();
            if (slider) {
                int sMin = slider->minimum(); // always 0
                // Recover step from range
                // We need meta info; use the spin range
                int rawMin = spin->minimum();
                int rawMax = spin->maximum();
                int steps = slider->maximum();
                int step = steps > 0 ? (rawMax - rawMin) / steps : 1;
                int pos = step > 0 ? (rawVal - rawMin) / step : 0;
                pos = qBound(0, pos, slider->maximum());
                slider->blockSignals(true);
                slider->setValue(pos);
                slider->blockSignals(false);
                QLabel* valLbl = settingSliderValLabels_.value(key, nullptr);
                if (valLbl) valLbl->setText(QString::number(rawVal));
                continue;
            }
        }

        // Checkbox
        if (auto cb = qobject_cast<QCheckBox*>(fw)) {
            cb->blockSignals(true);
            cb->setChecked(rawVal != 0);
            cb->blockSignals(false);
            continue;
        }
    }
}

void MainWindow::applyLayoutVisibility(int layoutVersion) {
    // THRESHOLD_96TO48 and THRESHOLD_48TO24: visible only when layoutVersion >= 209 AND PWM_FREQUENCY == 0 (Dynamic)
    int pwmVal = settingSpins_.contains("PWM_FREQUENCY") ? settingSpins_["PWM_FREQUENCY"]->value() : 24;
    bool showThresholds = (layoutVersion >= 209) && (pwmVal == 0);
    for (const QString& key : {"THRESHOLD_96TO48", "THRESHOLD_48TO24"}) {
        if (settingStacks_.contains(key)) {
            // Hide the entire row by hiding all widgets in that grid row
            settingStacks_[key]->parentWidget()->setVisible(showThresholds);
        }
    }

    if (settingFriendly_.contains("DITHERING")) {
        QCheckBox* cb = qobject_cast<QCheckBox*>(settingFriendly_["DITHERING"]);
        if (cb) {
            bool deprecated = (layoutVersion >= 208);
            cb->setEnabled(!deprecated);
            cb->setToolTip(deprecated ? QString::fromUtf8("Deprecated in Bluejay layout \u2265208 (may be ignored by firmware).") : "");
        }
    }

    // MOTOR_DIRECTION 3D warning
    if (motorDir3dWarning_ && settingSpins_.contains("MOTOR_DIRECTION")) {
        int dirVal = settingSpins_["MOTOR_DIRECTION"]->value();
        motorDir3dWarning_->setVisible(friendlyMode_ && (dirVal == 3 || dirVal == 4));
        if (settingFriendly_.contains("MOTOR_DIRECTION")) {
            QComboBox* combo = qobject_cast<QComboBox*>(settingFriendly_["MOTOR_DIRECTION"]);
            if (combo) combo->setEnabled(!(dirVal == 3 || dirVal == 4));
        }
    }
}

void MainWindow::onFriendlyToggled(bool friendly) {
    friendlyMode_ = friendly;
    int stackIdx = friendly ? 0 : 1;
    for (auto it = settingStacks_.begin(); it != settingStacks_.end(); ++it) {
        it.value()->setCurrentIndex(stackIdx);
    }
    // Re-sync friendly widgets from raw values when switching to friendly
    if (friendly) {
        syncFriendlyFromRaw();
    }
    // Update visibility (3D warning etc.)
    if (selectedEscIndex_ >= 0) {
        EscState esc = escModel_->escAt(selectedEscIndex_);
        int lv = esc.identity ? esc.identity->layoutVersion : 200;
        applyLayoutVisibility(lv);
    }
}

// ========== Bundled Bluejay firmware resolver ==========

QString MainWindow::normalizeTargetToSlug(const QString& target) {
    QString s = target;
    s.remove('#');
    s.replace('-', '_');
    QStringList parts = s.split('_', Qt::SkipEmptyParts);
    if (!parts.isEmpty()) {
        bool ok = false;
        int num = parts.last().toInt(&ok);
        if (ok) {
            parts.last() = QString::number(num); // strips leading zeros
        }
    }
    return parts.join('_');
}

QString MainWindow::findBundledBluejayHex(const EscIdentity& id, const QString& version) {
    QString slug = normalizeTargetToSlug(id.target);
    int pwm = id.pwmKhz;
    if (pwm != 24 && pwm != 48 && pwm != 96) return {};

    QString filename = QString("%1_%2_v%3.hex").arg(slug).arg(pwm).arg(version);
    QString verDir = QString("v%1").arg(version);

    QString appDir = QCoreApplication::applicationDirPath();
    QStringList searchBases;
    searchBases << appDir + "/../bluejay_firmware/" + verDir;
    searchBases << appDir + "/tools/bluejay_firmware/" + verDir;
    searchBases << appDir + "/../../tools/bluejay_firmware/" + verDir;
    searchBases << "tools/bluejay_firmware/" + verDir;

    for (const QString& base : searchBases) {
        QString candidate = QDir(base).absoluteFilePath(filename);
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

void MainWindow::maybeAutoPickBundledFirmware() {
    if (!useBundledCheck_->isChecked()) return;

    // Determine reference ESC identity
    std::optional<EscIdentity> refId;
    if (!flashApplyAllCheck_->isChecked() && selectedEscIndex_ >= 0) {
        EscState esc = escModel_->escAt(selectedEscIndex_);
        if (esc.identity) refId = esc.identity;
    } else if (flashApplyAllCheck_->isChecked()) {
        QList<int> indices = escModel_->indicesWithBaseSettings();
        for (int idx : indices) {
            EscState esc = escModel_->escAt(idx);
            if (esc.identity) {
                refId = esc.identity;
                break;
            }
        }
    }

    if (!refId) {
        firmwarePathEdit_->clear();
        firmwareStatusLabel_->setText("Waiting for ESC read...");
        firmwareStatusLabel_->setStyleSheet("color: #6b7280;");
        return;
    }

    QString version = bluejayVersionCombo_->currentData().toString();
    QString hexPath = findBundledBluejayHex(*refId, version);

    if (!hexPath.isEmpty()) {
        firmwarePathEdit_->setText(hexPath);
        QString fname = QFileInfo(hexPath).fileName();
        firmwareStatusLabel_->setText(QString::fromUtf8("\u2713 Bundled: %1").arg(fname));
        firmwareStatusLabel_->setStyleSheet("color: #16a34a;");
    } else {
        firmwarePathEdit_->clear();
        QString slug = normalizeTargetToSlug(refId->target);
        firmwareStatusLabel_->setText(
            QString::fromUtf8("\u2717 Bundled firmware not found for target=%1 pwm=%2 version=%3")
                .arg(slug).arg(refId->pwmKhz).arg(version));
        firmwareStatusLabel_->setStyleSheet("color: #dc2626;");
    }
}

int MainWindow::normalizeRwIndex(int escIndex1Based) {
    return escIndex1Based - 1;
}

int MainWindow::normalizeFlashIndex(int index0Based) {
    return index0Based;
}

void MainWindow::onRefreshPorts() {
    portCombo_->clear();
    appendLog(QString("CLI path: %1").arg(cli_->cliPath()));
    QList<PortInfo> ports = cli_->listPortsSync();
    if (ports.isEmpty()) {
        appendLog("No ports found (check if CLI exists and serial devices are connected)");
    } else {
        appendLog(QString("Found %1 port(s)").arg(ports.size()));
    }
    for (const auto& p : ports) {
        QString text = p.port;
        if (!p.description.isEmpty()) text += " - " + p.description;
        portCombo_->addItem(text, p.port);
    }
    updateUiState();
}

void MainWindow::onConnectAndRead() {
    if (portCombo_->currentData().isNull()) return;

    escModel_->clear();
    selectedEscIndex_ = -1;
    readGateOpen_ = false;
    uiHelloReceived_ = false;
    flashEventsReceived_ = false;
    clearWarning();
    logView_->clear();

    ConnectionOptions conn;
    conn.port = portCombo_->currentData().toString();
    conn.baud = baudSpin_->value();
    conn.settleMs = settleMsSpin_->value();
    conn.trace = traceCheck_->isChecked();

    ReadOptions opts;
    opts.all = true;
    opts.readRounds = readRoundsSpin_->value();
    opts.readRoundSleepMs = readRoundSleepSpin_->value();

    if (cli_->startReadAll(conn, opts)) {
        activeOp_ = OpKind::Read;
        helloTimer_->start(1500);
        updateUiState();
    } else {
        appendLog("Failed to start read operation");
    }
}

void MainWindow::onCancel() {
    writeBatchCancelled_ = true;
    cli_->cancel();
    appendLog("Cancel requested");
}

void MainWindow::onEscSelectionChanged(const QModelIndex& current, const QModelIndex& /*previous*/) {
    if (current.isValid()) {
        selectedEscIndex_ = current.row();
    } else {
        selectedEscIndex_ = -1;
    }
    updateSettingsPanel();
    updateUiState();
    maybeAutoPickBundledFirmware();
}

void MainWindow::onUiEvent(const QJsonObject& event) {
    QString type = event.value("type").toString();

    if (type == "ui_hello") handleUiHello(event);
    else if (type == "passthrough_info") handlePassthroughInfo(event);
    else if (type == "mapping_resolved") handleMappingResolved(event);
    else if (type == "esc_reachability") handleEscReachability(event);
    else if (type == "esc_read_start") handleEscReadStart(event);
    else if (type == "esc_read_ok") handleEscReadOk(event);
    else if (type == "esc_read_fail") handleEscReadFail(event);
    else if (type == "esc_read_degraded") handleEscReadDegraded(event);
    else if (type == "esc_read_summary") handleEscReadSummary(event);
    else if (type == "esc_select_fail") handleEscSelectFail(event);
    else if (type == "esc_settings") handleEscSettings(event);
    else if (type == "esc_write_start") handleEscWriteStart(event);
    else if (type == "esc_write_ok") handleEscWriteOk(event);
    else if (type == "esc_write_fail") handleEscWriteFail(event);
    else if (type == "flash_plan") handleFlashPlan(event);
    else if (type == "esc_flash_start") handleEscFlashStart(event);
    else if (type == "esc_flash_ok") handleEscFlashOk(event);
    else if (type == "esc_flash_fail") handleEscFlashFail(event);
    else if (type == "esc_flash_skipped") handleEscFlashSkipped(event);
    else if (type == "msp_restore") handleMspRestore(event);
    else if (type == "op_done") handleOpDone(event);
    // C2 events
    else if (type == "c2_detect_ok") handleC2DetectOk(event);
    else if (type == "c2_detect_fail") handleC2DetectFail(event);
    else if (type == "c2_target_info") handleC2TargetInfo(event);
    else if (type == "c2_read_info_fail") handleC2ReadInfoFail(event);
    else if (type == "c2_erase_ok") handleC2EraseOk(event);
    else if (type == "c2_erase_fail") handleC2EraseFail(event);
    else if (type == "c2_write_progress") handleC2WriteProgress(event);
    else if (type == "c2_write_ok") handleC2WriteOk(event);
    else if (type == "c2_write_fail") handleC2WriteFail(event);
    else if (type == "c2_install_ok") handleC2InstallOk(event);
    else if (type == "c2_install_fail") handleC2InstallFail(event);
    else if (type == "c2_install_progress") handleC2InstallProgress(event);
    else if (traceCheck_->isChecked()) {
        appendLog(QString("[ndjson] unhandled: %1").arg(type));
    }
}

void MainWindow::onLogLine(const QString& /*stream*/, const QString& text) {
    appendLog(text);
}

void MainWindow::onProcessExited(int code, bool cancelled) {
    helloTimer_->stop();

    if (activeOp_ == OpKind::Flash) {
        QString reason = cancelled ? "Cancelled" : "No result (operation ended)";
        escModel_->finalizeQueuedOrFlashing(reason);

        if (!flashEventsReceived_ && !flashTargets_.isEmpty()) {
            showWarning("Flashing produced no per-ESC events; status may be inaccurate. Check CLI version.");
        }
        flashTargets_.clear();
    }

    // Finish write batch (now single CLI invocation handles all ESCs)
    if (!writeQueue_.isEmpty()) {
        finishWriteBatch(!cancelled && code == 0);
    }

    activeOp_ = OpKind::None;

    appendLog(QString("Process exited with code %1%2").arg(code).arg(cancelled ? " (cancelled)" : ""));
    updateUiState();
}

void MainWindow::onPickFirmware() {
    QString path = QFileDialog::getOpenFileName(this, "Select Firmware",
        QString(), "Intel HEX (*.hex);;All Files (*)");
    if (path.isEmpty()) return;

    firmwarePathEdit_->setText(path);
    settings_.setValue("firmwarePath", path);

    if (QFileInfo::exists(path)) {
        firmwareStatusLabel_->setText("✓ File exists");
        firmwareStatusLabel_->setStyleSheet("color: #16a34a;");
    } else {
        firmwareStatusLabel_->setText("✗ File not found");
        firmwareStatusLabel_->setStyleSheet("color: #dc2626;");
    }
    updateUiState();
}

void MainWindow::onClearFirmware() {
    firmwarePathEdit_->clear();
    firmwareStatusLabel_->clear();
    settings_.setValue("firmwarePath", "");
    updateUiState();
}

void MainWindow::onStartFlash() {
    QString fwPath = firmwarePathEdit_->text();
    if (fwPath.isEmpty() || !QFileInfo::exists(fwPath)) return;

    if (!flashApplyAllCheck_->isChecked() && selectedEscIndex_ >= 0) {
        auto state = escModel_->escAt(selectedEscIndex_);
        if (state.reachable.has_value() && !state.reachable.value()) {
            auto answer = QMessageBox::warning(this, "ESC Unreachable",
                QString("ESC%1 appears unreachable via passthrough.\n"
                        "Flash will likely fail. Consider using the C2 tab for direct flashing.\n\n"
                        "Continue anyway?").arg(selectedEscIndex_ + 1),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
            if (answer != QMessageBox::Yes) return;
        }
    }

    if (flashApplyAllCheck_->isChecked() && useBundledCheck_->isChecked()) {
        QList<int> indices = escModel_->indicesWithBaseSettings();
        QString refSlug;
        int refPwm = 0;
        bool mixed = false;
        for (int idx : indices) {
            EscState esc = escModel_->escAt(idx);
            if (!esc.identity) continue;
            QString slug = normalizeTargetToSlug(esc.identity->target);
            int pwm = esc.identity->pwmKhz;
            if (refSlug.isEmpty()) { refSlug = slug; refPwm = pwm; }
            else if (slug != refSlug || pwm != refPwm) { mixed = true; break; }
        }
        if (mixed) {
            auto answer = QMessageBox::warning(this, "Mixed ESC Targets",
                "ESCs have different targets or PWM frequencies.\n"
                "A single bundled hex file may not match all ESCs.\n\n"
                "Consider flashing ESCs individually, or switch to manual firmware selection.\n\n"
                "Continue anyway with the current hex?",
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
            if (answer != QMessageBox::Yes) return;
        }
    }

    bool needsConfirm = flashApplyAllCheck_->isChecked() ||
                        flashEraseEepromCheck_->isChecked() ||
                        flashFullEraseAppCheck_->isChecked() ||
                        flashFullEraseEntireAppCheck_->isChecked() ||
                        flashVerifyAllBytesCheck_->isChecked();

    if (needsConfirm) {
        QString msg = "Flash ";
        if (flashApplyAllCheck_->isChecked()) {
            msg += QString("%1 ESCs").arg(escModel_->indicesWithBaseSettings().size());
        } else {
            msg += QString("ESC%1").arg(selectedEscIndex_ + 1);
        }
        msg += " with " + QFileInfo(fwPath).fileName() + "?";

        if (QMessageBox::question(this, "Confirm Flash", msg) != QMessageBox::Yes) {
            return;
        }
    }

    uiHelloReceived_ = false;
    flashEventsReceived_ = false;
    clearWarning();
    flashTargets_.clear();
    lastFlashWasDryRun_ = flashDryRunCheck_->isChecked();
    flashOkCount_ = 0;

    QList<int> targets;
    if (flashApplyAllCheck_->isChecked()) {
        targets = escModel_->indicesWithBaseSettings();
    } else if (selectedEscIndex_ >= 0) {
        targets.append(selectedEscIndex_);
    }

    for (int idx : targets) {
        escModel_->setEscStatus(idx, EscStatus::Queued);
        flashTargets_.insert(idx);
    }

    flashProgressLabel_->setText(QString("Progress: 0/%1").arg(targets.size()));

    ConnectionOptions conn;
    conn.port = portCombo_->currentData().toString();
    conn.baud = baudSpin_->value();
    conn.settleMs = settleMsSpin_->value();
    conn.trace = traceCheck_->isChecked();

    FlashOptions opts;
    opts.hexPath = fwPath;
    opts.targetAll = flashApplyAllCheck_->isChecked();
    opts.targetIndex = flashApplyAllCheck_->isChecked() ? -1 : (selectedEscIndex_ + 1);
    opts.verify = flashVerifyCombo_->currentData().toString();
    opts.eraseEeprom = flashEraseEepromCheck_->isChecked();
    opts.fullEraseApp = flashFullEraseAppCheck_->isChecked();
    opts.fullEraseEntireApp = flashFullEraseEntireAppCheck_->isChecked();
    opts.verifyAllBytes = flashVerifyAllBytesCheck_->isChecked();
    opts.dryRun = flashDryRunCheck_->isChecked();
    opts.skipMissing = flashSkipMissingCheck_->isChecked();
    opts.slowSwitching = flashSlowSwitchingCheck_->isChecked();
    opts.assumeSig = flashAssumeSigEdit_->text();
    opts.eraseRetries = flashEraseRetriesSpin_->value();
    opts.writeRetries = flashWriteRetriesSpin_->value();
    opts.interEscMs = flashInterEscMsSpin_->value();
    opts.postSelectMs = flashPostSelectMsSpin_->value();
    opts.verifyReadRetries = flashVerifyReadRetriesSpin_->value();

    if (cli_->startFlash(conn, opts)) {
        activeOp_ = OpKind::Flash;
        helloTimer_->start(1500);
        updateUiState();
    } else {
        appendLog("Failed to start flash operation");
        for (int idx : targets) {
            escModel_->setEscStatus(idx, EscStatus::Fail, "Failed to start");
        }
    }
}

void MainWindow::onWriteSettings() {
    if (pendingWrites_.isEmpty()) return;

    writeQueue_.clear();
    writeQueueIndex_ = 0;
    writeBatchCancelled_ = false;

    bool applyAll = settingsApplyAllCheck_->isChecked();

    if (applyAll) {
        writeQueue_ = escModel_->indicesWithBaseSettings();
    } else if (selectedEscIndex_ >= 0) {
        writeQueue_.append(selectedEscIndex_);
    }

    if (writeQueue_.isEmpty()) return;

    // Mark all targets as Writing
    for (int idx : writeQueue_) {
        escModel_->setEscStatus(idx, EscStatus::Writing);
    }

    uiHelloReceived_ = false;
    clearWarning();

    ConnectionOptions conn;
    conn.port = portCombo_->currentData().toString();
    conn.baud = baudSpin_->value();
    conn.settleMs = settleMsSpin_->value();
    conn.trace = traceCheck_->isChecked();

    QList<QPair<QString, QVariant>> sets;
    for (auto it = pendingWrites_.begin(); it != pendingWrites_.end(); ++it) {
        sets.append({it.key(), it.value()});
    }

    bool started = false;
    if (applyAll) {
        // Single CLI invocation for all ESCs
        started = cli_->startWriteSettingsAll(conn, sets);
    } else {
        // Single ESC write
        started = cli_->startWriteSettings(conn, selectedEscIndex_ + 1, sets);
    }

    if (started) {
        activeOp_ = OpKind::WriteSettings;
        helloTimer_->start(1500);
        updateUiState();
    } else {
        appendLog("Failed to start write operation");
        for (int idx : writeQueue_) {
            escModel_->setEscStatus(idx, EscStatus::Fail, "Failed to start write");
        }
        writeQueue_.clear();
    }
}

void MainWindow::doWriteNextEsc() {
    // No longer used - kept for compatibility but write is now single CLI invocation
    finishWriteBatch(true);
}

void MainWindow::finishWriteBatch(bool success) {
    writeQueue_.clear();
    writeQueueIndex_ = 0;
    activeOp_ = OpKind::None;

    if (success) {
        pendingWrites_.clear();
        updateDirtyIndicators();
    }
    updateUiState();
}

void MainWindow::onSettingChanged() {
    updateDirtyIndicators();
}

bool MainWindow::isCommFailure(const QString& error) {
    if (error.isEmpty()) return false;
    static QRegularExpression re("(select|initflash|invalid_channel|general_error|no response|timeout|timed out|failed to open)",
                                  QRegularExpression::CaseInsensitiveOption);
    return re.match(error).hasMatch();
}

void MainWindow::handleUiHello(const QJsonObject& ev) {
    uiHelloReceived_ = true;
    helloTimer_->stop();
    clearWarning();
    if (traceCheck_->isChecked()) {
        int pid = ev.value("pid").toInt(-1);
        appendLog(QString("[ndjson] ui_hello received (pid=%1)").arg(pid));
    }
}

void MainWindow::handlePassthroughInfo(const QJsonObject& ev) {
    int count = ev.value("esc_count").toInt(0);
    // Seed sidebar placeholders immediately so they're visible even if mapping fails
    if (count > 0 && escModel_->escCount() == 0) {
        escModel_->seedEscs(count);
    }
}

void MainWindow::handleMappingResolved(const QJsonObject& ev) {
    int count = ev.value("esc_count").toInt(0);
    // Only seed ESCs if we don't already have them (avoid resetting during write batch)
    if (count > 0 && escModel_->escCount() == 0) {
        escModel_->seedEscs(count);
    }
}

void MainWindow::handleEscReachability(const QJsonObject& ev) {
    int idx = ev.value("index").toInt(-1);
    bool reachable = ev.value("reachable").toBool(false);
    if (idx >= 0 && idx < escModel_->rowCount()) {
        escModel_->setEscReachable(idx, reachable);
        if (!reachable) {
            escModel_->setEscStatus(idx, EscStatus::Fail, "unreachable");
        }
    }
}

void MainWindow::handleEscReadStart(const QJsonObject& ev) {
    int idx = normalizeRwIndex(ev.value("esc_index").toInt());
    escModel_->setEscStatus(idx, EscStatus::Reading);
}

void MainWindow::handleEscReadOk(const QJsonObject& ev) {
    int idx = normalizeRwIndex(ev.value("esc_index").toInt());
    escModel_->setEscStatus(idx, EscStatus::Ok);
    escModel_->setEscReachable(idx, true);
    maybeAutoPickBundledFirmware();
}

void MainWindow::handleEscReadFail(const QJsonObject& ev) {
    int idx = normalizeRwIndex(ev.value("esc_index").toInt());
    QString error = ev.value("error").toString("read failed");
    // Don't overwrite status if this is an intermediate round failure (will be retried)
    int round = ev.value("round").toInt(0);
    if (round > 0) {
        // Multi-round read - only mark fail if it's not going to be retried
        // The esc_read_summary event will set the final status
    } else {
        escModel_->setEscStatus(idx, EscStatus::Fail, error);
        escModel_->setEscReachable(idx, false);
    }
}

void MainWindow::handleEscReadDegraded(const QJsonObject& ev) {
    int idx = normalizeRwIndex(ev.value("esc_index").toInt());
    escModel_->setEscStatus(idx, EscStatus::Unstable, "read via direct addressing (identity not guaranteed)");
    if (traceCheck_->isChecked()) {
        appendLog(QString("[ndjson] ESC%1: read degraded (used direct addressing)").arg(idx + 1));
    }
}

void MainWindow::handleEscSelectFail(const QJsonObject& ev) {
    if (traceCheck_->isChecked()) {
        int idx = ev.value("esc_index").toInt();
        QString mapping = ev.value("mapping_mode").toString();
        int attempts = ev.value("attempts").toInt();
        QString error = ev.value("error").toString();
        appendLog(QString("[diag] ESC%1 select failed: mapping=%2 attempts=%3 error=%4")
            .arg(idx).arg(mapping).arg(attempts).arg(error));
    }
}

void MainWindow::handleEscReadSummary(const QJsonObject& ev) {
    int idx = normalizeRwIndex(ev.value("esc_index").toInt());
    int successRounds = ev.value("success_rounds").toInt(0);
    int totalRounds = ev.value("total_rounds").toInt(0);
    bool stable = ev.value("stable").toBool(false);
    bool skipped = ev.value("skipped").toBool(false);

    if (skipped) {
        escModel_->setEscStatus(idx, EscStatus::Fail, "unreachable");
        escModel_->setEscReachable(idx, false);
    } else if (successRounds == 0) {
        escModel_->setEscStatus(idx, EscStatus::Fail, "read failed");
    } else if (!stable) {
        escModel_->setEscStatus(idx, EscStatus::Unstable,
            QString("recovered (%1/%2 rounds)").arg(successRounds).arg(totalRounds));
    }
    // stable OK is already set by handleEscReadOk + handleEscSettings
}

void MainWindow::handleEscSettings(const QJsonObject& ev) {
    int idx = normalizeRwIndex(ev.value("esc_index").toInt());
    QString sig = ev.value("sig").toString();

    EscIdentity identity;
    QJsonObject idObj = ev.value("identity").toObject();
    identity.layoutVersion = idObj.value("layout_version").toInt();
    identity.fw = idObj.value("fw").toString();
    identity.target = idObj.value("target").toString();
    identity.pwmKhz = idObj.value("pwm_khz").toInt();

    QMap<QString, int> settings;
    QJsonObject settingsObj = ev.value("settings").toObject();
    for (auto it = settingsObj.begin(); it != settingsObj.end(); ++it) {
        settings[it.key()] = it.value().toInt();
    }

    EscState esc = escModel_->escAt(idx);
    QMap<QString, int> baseSettings = esc.hasBaseSettings() ? esc.baseSettings : settings;

    escModel_->setEscIdentity(idx, sig, identity);
    escModel_->setEscSettings(idx, settings, baseSettings);
    escModel_->setEscStatus(idx, EscStatus::Ok);
    escModel_->setEscReachable(idx, true);

    readGateOpen_ = true;

    if (selectedEscIndex_ < 0) {
        selectedEscIndex_ = idx;
        escListView_->setCurrentIndex(escModel_->index(idx));
    }
    if (selectedEscIndex_ == idx) {
        updateSettingsPanel();
    }
    updateUiState();
}

void MainWindow::handleEscWriteStart(const QJsonObject& ev) {
    if (activeOp_ == OpKind::Flash) return;
    int idx = normalizeRwIndex(ev.value("esc_index").toInt());
    escModel_->setEscStatus(idx, EscStatus::Writing);
    escModel_->setEscReachable(idx, true);
}

void MainWindow::handleEscWriteOk(const QJsonObject& ev) {
    int idx = normalizeRwIndex(ev.value("esc_index").toInt());

    if (activeOp_ == OpKind::Flash) {
        EscState esc = escModel_->escAt(idx);
        if (esc.hasSettings()) {
            escModel_->setEscSettings(idx, esc.settings, esc.settings);
        }
        return;
    }

    EscState esc = escModel_->escAt(idx);
    QMap<QString, int> newSettings = esc.settings;
    for (auto it = pendingWrites_.begin(); it != pendingWrites_.end(); ++it) {
        newSettings[it.key()] = it.value();
    }
    escModel_->setEscSettings(idx, newSettings, newSettings);
    escModel_->setEscStatus(idx, EscStatus::Ok);
    escModel_->setEscReachable(idx, true);
    appendLog(QString("ESC%1 write OK").arg(idx + 1));

    // Refresh settings panel if this is the selected ESC
    if (idx == selectedEscIndex_) {
        updateSettingsPanel();
    }
}

void MainWindow::handleEscWriteFail(const QJsonObject& ev) {
    if (activeOp_ == OpKind::Flash) return;
    int idx = normalizeRwIndex(ev.value("esc_index").toInt());
    escModel_->setEscStatus(idx, EscStatus::Fail, "write failed");
    escModel_->setEscReachable(idx, false);
    appendLog(QString("ESC%1 write FAIL").arg(idx + 1));
}

void MainWindow::handleFlashPlan(const QJsonObject& ev) {
    QString targets = ev.value("targets").toString();
    QString hexName = ev.value("hex_name").toString();
    QString verify = ev.value("verify").toString();

    QList<int> targetList;
    if (targets == "all") {
        for (int i = 0; i < escModel_->escCount(); ++i) {
            targetList.append(i);
        }
    } else if (ev.contains("index")) {
        int idx = normalizeFlashIndex(ev.value("index").toInt());
        targetList.append(idx);
    }

    flashTargets_.clear();
    for (int idx : targetList) {
        escModel_->setEscStatus(idx, EscStatus::Queued);
        flashTargets_.insert(idx);
    }

    QString tgt = targets == "all" ? QString("all (%1)").arg(escModel_->escCount())
                                   : QString("ESC%1").arg(targetList.isEmpty() ? 0 : targetList[0] + 1);
    appendLog(QString("flash plan: %1 verify=%2 target=%3").arg(hexName, verify, tgt));
}

void MainWindow::handleEscFlashStart(const QJsonObject& ev) {
    flashEventsReceived_ = true;
    int idx = normalizeFlashIndex(ev.value("index").toInt());
    if (traceCheck_->isChecked()) {
        appendLog(QString("[ndjson] esc_flash_start index=%1 -> idx=%2").arg(ev.value("index").toInt()).arg(idx));
    }
    escModel_->setEscStatus(idx, EscStatus::Flashing);
    escModel_->setEscReachable(idx, true);
    appendLog(QString("ESC%1 flash started").arg(idx + 1));
}

void MainWindow::handleEscFlashOk(const QJsonObject& ev) {
    flashEventsReceived_ = true;
    int idx = normalizeFlashIndex(ev.value("index").toInt());
    if (traceCheck_->isChecked()) {
        appendLog(QString("[ndjson] esc_flash_ok index=%1 -> idx=%2").arg(ev.value("index").toInt()).arg(idx));
    }
    escModel_->setEscStatus(idx, EscStatus::Ok);
    escModel_->setEscReachable(idx, true);
    flashTargets_.remove(idx);
    flashOkCount_++;
    appendLog(QString("ESC%1 flash OK").arg(idx + 1));

    int total = flashTargets_.size() + 1;
    int done = total - flashTargets_.size();
    flashProgressLabel_->setText(QString("Progress: %1/%2").arg(done).arg(total));
}

void MainWindow::handleEscFlashFail(const QJsonObject& ev) {
    flashEventsReceived_ = true;
    int idx = normalizeFlashIndex(ev.value("index").toInt());
    QString error = ev.value("error").toString("flash failed");
    if (traceCheck_->isChecked()) {
        appendLog(QString("[ndjson] esc_flash_fail index=%1 -> idx=%2 error=%3").arg(ev.value("index").toInt()).arg(idx).arg(error));
    }
    escModel_->setEscStatus(idx, EscStatus::Fail, error);
    escModel_->setEscReachable(idx, !isCommFailure(error));
    flashTargets_.remove(idx);
    appendLog(QString("ESC%1 flash FAIL: %2").arg(idx + 1).arg(error));
}

void MainWindow::handleEscFlashSkipped(const QJsonObject& ev) {
    flashEventsReceived_ = true;
    int idx = normalizeFlashIndex(ev.value("index").toInt());
    QString reason = ev.value("reason").toString("skipped");
    escModel_->setEscStatus(idx, EscStatus::Fail, reason);
    escModel_->setEscReachable(idx, false);
    flashTargets_.remove(idx);
    appendLog(QString("ESC%1 flash skipped: %2").arg(idx + 1).arg(reason));
}

void MainWindow::handleMspRestore(const QJsonObject& ev) {
    bool ok = ev.value("ok").toBool();
    int attempts = ev.value("attempts").toInt();
    int elapsed = ev.value("elapsed_ms").toInt();
    QString lastError = ev.value("last_error").toString();

    if (ok) {
        appendLog(QString("MSP restored after %1 attempts / %2 ms").arg(attempts).arg(elapsed));
    } else {
        showWarning("FC not ready after passthrough exit. Wait a moment or power cycle before next operation.");
        appendLog(QString("MSP restore FAILED after %1 attempts / %2 ms: %3")
            .arg(attempts).arg(elapsed).arg(lastError));
    }
}

void MainWindow::handleOpDone(const QJsonObject& ev) {
    QString op = ev.value("op").toString();
    bool success = ev.value("success").toBool();

    if (op == "flash") {
        QString reason = "No result (operation ended)";
        escModel_->finalizeQueuedOrFlashing(reason);
        flashTargets_.clear();

        if (success && !lastFlashWasDryRun_ && flashOkCount_ > 0) {
            showFlashPostWriteNotice();
        }
    }

    activeOp_ = OpKind::None;
    updateUiState();
}

// ========== C2 Slots ==========

void MainWindow::onC2RefreshPorts() {
    c2PortCombo_->clear();
    auto ports = cli_->listPortsSync();
    for (const auto& p : ports) {
        QString label = p.port;
        if (!p.description.isEmpty()) label += " - " + p.description;
        c2PortCombo_->addItem(label, p.port);
    }
}

void MainWindow::onC2Detect() {
    if (cli_->isBusy()) return;
    QString port = c2PortCombo_->currentData().toString();
    if (port.isEmpty()) {
        showWarning("Select an Arduino port first");
        return;
    }

    clearWarning();
    c2InterfaceStatusLabel_->setText("Detecting...");
    c2InterfaceStatusLabel_->setStyleSheet("color: #71717a;");
    uiHelloReceived_ = false;

    if (cli_->startC2Detect(port)) {
        activeOp_ = OpKind::C2Detect;
        helloTimer_->start(1500);
        updateUiState();
    }
}

void MainWindow::onC2InstallUno() {
    if (cli_->isBusy()) return;
    QString port = c2PortCombo_->currentData().toString();
    if (port.isEmpty()) {
        showWarning("Select an Arduino port first");
        return;
    }

    clearWarning();
    c2InterfaceStatusLabel_->setText("Installing UNO firmware...");
    c2InterfaceStatusLabel_->setStyleSheet("color: #71717a;");
    uiHelloReceived_ = false;

    if (cli_->startC2Install(port, "uno")) {
        activeOp_ = OpKind::C2Install;
        helloTimer_->start(1500);
        updateUiState();
    }
}

void MainWindow::onC2InstallNano() {
    if (cli_->isBusy()) return;
    QString port = c2PortCombo_->currentData().toString();
    if (port.isEmpty()) {
        showWarning("Select an Arduino port first");
        return;
    }

    clearWarning();
    c2InterfaceStatusLabel_->setText("Installing Nano firmware...");
    c2InterfaceStatusLabel_->setStyleSheet("color: #71717a;");
    uiHelloReceived_ = false;

    if (cli_->startC2Install(port, "nano")) {
        activeOp_ = OpKind::C2Install;
        helloTimer_->start(1500);
        updateUiState();
    }
}

void MainWindow::onC2ReadInfo() {
    if (cli_->isBusy()) return;
    QString port = c2PortCombo_->currentData().toString();
    if (port.isEmpty()) {
        showWarning("Select an Arduino port first");
        return;
    }

    clearWarning();
    c2DeviceInfoLabel_->setText("Reading...");
    c2DeviceInfoLabel_->setStyleSheet("color: #71717a;");
    uiHelloReceived_ = false;

    if (cli_->startC2ReadInfo(port)) {
        activeOp_ = OpKind::C2ReadInfo;
        helloTimer_->start(1500);
        updateUiState();
    }
}

void MainWindow::onC2PickHex() {
    QString startDir;
    QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates = {
        appDir + "/../../dist/tools/bluejay_firmware",
        appDir + "/../bluejay_firmware",
        appDir + "/../../tools/bluejay_firmware",
        appDir + "/tools/bluejay_firmware"
    };
    for (const QString& c : candidates) {
        if (QDir(c).exists()) { startDir = QDir(c).absolutePath(); break; }
    }
    QString path = QFileDialog::getOpenFileName(this, "Select Firmware",
        startDir, "Intel HEX (*.hex);;All Files (*)");
    if (path.isEmpty()) return;

    c2HexPathEdit_->setText(path);
    settings_.setValue("c2HexPath", path);
}

void MainWindow::onC2ClearHex() {
    c2HexPathEdit_->clear();
    settings_.remove("c2HexPath");
}

void MainWindow::onC2Write() {
    if (cli_->isBusy()) return;
    
    QString port = c2PortCombo_->currentData().toString();
    QString hexPath = c2HexPathEdit_->text();

    if (port.isEmpty()) {
        showWarning("Select an Arduino port first");
        return;
    }
    if (hexPath.isEmpty()) {
        showWarning("Select a firmware file first");
        return;
    }
    if (!QFileInfo::exists(hexPath)) {
        showWarning("Firmware file not found");
        return;
    }

    clearWarning();
    bool isBatch = c2ModeCombo_->currentData().toString() == "batch";

    if (isBatch && !c2WizardActive_) {
        // Start batch mode
        c2WizardTotal_ = c2EscCountSpin_->value();
        c2WizardCurrent_ = 0;
        c2WizardActive_ = true;
        c2WizardStatusLabel_->setVisible(true);
        c2WizardStatusLabel_->setText(QString("Connect ESC #1, then click 'Flash ESC #1'\n\nProgress: 0/%1").arg(c2WizardTotal_));
        c2WizardStatusLabel_->setStyleSheet("color: #2563eb; padding: 8px;");
        c2WriteBtn_->setText("Flash ESC #1");
        c2WriteBtn_->setStyleSheet("QPushButton { background-color: #16a34a; color: white; padding: 8px 16px; border-radius: 4px; } QPushButton:hover { background-color: #15803d; } QPushButton:disabled { opacity: 0.5; }");
        c2CancelBatchBtn_->setVisible(true);
        appendLog(QString("C2 Batch: Starting flash of %1 ESCs").arg(c2WizardTotal_));
        return;
    }

    if (c2WizardActive_) {
        // Flash next ESC in batch
        c2BatchAdvance();
        return;
    }

    // Single mode - show pre-write advice, then flash
    if (!showC2PreWriteAdvice()) return;

    c2ProgressLabel_->setText("Starting...");
    c2ProgressBar_->setValue(0);
    c2ProgressBar_->setVisible(true);
    uiHelloReceived_ = false;

    if (cli_->startC2WriteHex(port, hexPath, -1)) {
        activeOp_ = OpKind::C2Write;
        helloTimer_->start(1500);
        updateUiState();
    }
}

void MainWindow::onC2ModeChanged(int index) {
    bool isBatch = c2ModeCombo_->currentData().toString() == "batch";
    c2EscCountLabel_->setVisible(isBatch);
    c2EscCountSpin_->setVisible(isBatch);
    
    // Reset batch state when switching modes
    if (!isBatch && c2WizardActive_) {
        c2WizardActive_ = false;
        c2WizardStatusLabel_->setVisible(false);
        c2CancelBatchBtn_->setVisible(false);
        c2WriteBtn_->setText("Erase + Write");
        c2WriteBtn_->setStyleSheet("QPushButton { background-color: #18181b; color: white; padding: 8px 16px; border-radius: 4px; } QPushButton:hover { background-color: #27272a; } QPushButton:disabled { opacity: 0.5; }");
    }
}

void MainWindow::onC2CancelBatch() {
    if (cli_->isBusy()) {
        cli_->cancel();
    }
    c2WizardActive_ = false;
    c2WizardStatusLabel_->setVisible(false);
    c2CancelBatchBtn_->setVisible(false);
    c2ProgressBar_->setVisible(false);
    c2ProgressLabel_->clear();
    c2WriteBtn_->setText("Erase + Write");
    c2WriteBtn_->setStyleSheet("QPushButton { background-color: #18181b; color: white; padding: 8px 16px; border-radius: 4px; } QPushButton:hover { background-color: #27272a; } QPushButton:disabled { opacity: 0.5; }");
    c2WriteBtn_->setEnabled(true);
    appendLog("C2 Batch: Cancelled");
}

void MainWindow::c2BatchAdvance() {
    QString port = c2PortCombo_->currentData().toString();
    QString hexPath = c2HexPathEdit_->text();

    // Show pre-write advice once at the start of batch (before ESC #1)
    if (c2WizardCurrent_ == 0) {
        if (!showC2PreWriteAdvice()) return;
    }

    c2WizardCurrent_++;
    int escNum = c2WizardCurrent_;

    c2WizardStatusLabel_->setText(QString("Flashing ESC #%1...\n\nProgress: %2/%3")
        .arg(escNum).arg(escNum - 1).arg(c2WizardTotal_));
    c2WizardStatusLabel_->setStyleSheet("color: #b45309; padding: 8px;");
    c2WriteBtn_->setEnabled(false);
    c2WriteBtn_->setText("Flashing...");

    c2ProgressLabel_->setText(QString("ESC #%1...").arg(escNum));
    c2ProgressBar_->setValue(0);
    c2ProgressBar_->setVisible(true);
    uiHelloReceived_ = false;

    appendLog(QString("C2 Batch: Flashing ESC #%1 of %2").arg(escNum).arg(c2WizardTotal_));

    if (cli_->startC2WriteHex(port, hexPath, escNum - 1)) {
        activeOp_ = OpKind::C2Write;
        helloTimer_->start(1500);
        updateUiState();
    } else {
        c2WizardStatusLabel_->setText(QString("Failed to start ESC #%1").arg(escNum));
        c2WizardStatusLabel_->setStyleSheet("color: #dc2626; padding: 8px;");
        c2WriteBtn_->setEnabled(true);
        c2WriteBtn_->setText("Retry ESC #" + QString::number(escNum));
        c2WizardCurrent_--;
    }
}

// ========== C2 Event Handlers ==========

void MainWindow::handleC2DetectOk(const QJsonObject& /*ev*/) {
    c2InterfaceStatusLabel_->setText("✓ Interface detected");
    c2InterfaceStatusLabel_->setStyleSheet("color: #16a34a;");
    appendLog("C2: Interface detected");
}

void MainWindow::handleC2DetectFail(const QJsonObject& ev) {
    QString error = ev.value("error").toString("Detection failed");
    c2InterfaceStatusLabel_->setText("✗ Not detected");
    c2InterfaceStatusLabel_->setStyleSheet("color: #dc2626;");
    appendLog("C2: Interface not detected - " + error);
}

void MainWindow::handleC2TargetInfo(const QJsonObject& ev) {
    QString deviceId = ev.value("device_id").toString();
    QString revision = ev.value("revision").toString();
    QString warning = ev.value("warning").toString();

    if (warning == "target_not_connected") {
        c2DeviceInfoLabel_->setText("⚠ Target not connected (ID=0xFF)");
        c2DeviceInfoLabel_->setStyleSheet("color: #b45309;");
    } else {
        c2DeviceInfoLabel_->setText(QString("Device ID: %1  Revision: %2").arg(deviceId, revision));
        c2DeviceInfoLabel_->setStyleSheet("color: #16a34a;");
    }
    appendLog(QString("C2: Device ID=%1 Revision=%2").arg(deviceId, revision));
}

void MainWindow::handleC2ReadInfoFail(const QJsonObject& ev) {
    QString error = ev.value("error").toString("Read info failed");
    c2DeviceInfoLabel_->setText("✗ " + error);
    c2DeviceInfoLabel_->setStyleSheet("color: #dc2626;");
    appendLog("C2: Read info failed - " + error);
}

void MainWindow::handleC2EraseOk(const QJsonObject& /*ev*/) {
    c2ProgressLabel_->setText("Erase OK, writing...");
    appendLog("C2: Erase OK");
    
    // If in batch mode, restore green button style (in case we were retrying after failure)
    if (c2WizardActive_) {
        c2WriteBtn_->setStyleSheet("QPushButton { background-color: #16a34a; color: white; padding: 8px 16px; border-radius: 4px; } QPushButton:hover { background-color: #15803d; } QPushButton:disabled { opacity: 0.5; }");
    }
}

void MainWindow::handleC2EraseFail(const QJsonObject& ev) {
    QString error = ev.value("error").toString("Erase failed");
    c2ProgressLabel_->setText("Erase failed - check battery");
    c2ProgressBar_->setVisible(false);
    appendLog("C2: Erase failed - " + error);

    if (selectedEscIndex_ >= 0) {
        escModel_->setEscStatus(selectedEscIndex_, EscStatus::Fail, error);
    }

    // Handle batch mode - allow retry
    if (c2WizardActive_) {
        int escNum = c2WizardCurrent_;
        c2WriteBtn_->setText(QString("Retry ESC #%1").arg(escNum));
        c2WriteBtn_->setStyleSheet("QPushButton { background-color: #dc2626; color: white; padding: 8px 16px; border-radius: 4px; } QPushButton:hover { background-color: #b91c1c; } QPushButton:disabled { opacity: 0.5; }");
        c2WriteBtn_->setEnabled(true);
        c2WizardStatusLabel_->setText(QString("Erase failed for ESC #%1\n\nCheck battery connection and retry\n\nProgress: %2/%3")
            .arg(escNum).arg(escNum - 1).arg(c2WizardTotal_));
        c2WizardStatusLabel_->setStyleSheet("color: #dc2626; padding: 8px;");
        c2WizardCurrent_--;  // Allow retry of same ESC
    } else {
        // Single mode - just re-enable button
        c2WriteBtn_->setEnabled(true);
    }
}

void MainWindow::handleC2WriteProgress(const QJsonObject& ev) {
    int bytesDone = ev.value("bytes_done").toInt();
    int bytesTotal = ev.value("bytes_total").toInt();
    int chunkIdx = ev.value("chunk_index").toInt();
    int chunksTotal = ev.value("chunks_total").toInt();

    if (bytesTotal > 0) {
        int pct = (bytesDone * 100) / bytesTotal;
        c2ProgressBar_->setValue(pct);
        c2ProgressLabel_->setText(QString("Writing: %1/%2 bytes (%3%)").arg(bytesDone).arg(bytesTotal).arg(pct));
    } else {
        c2ProgressLabel_->setText(QString("Writing chunk %1/%2").arg(chunkIdx).arg(chunksTotal));
    }
}

void MainWindow::handleC2WriteOk(const QJsonObject& ev) {
    c2ProgressLabel_->setText("✓ Write complete");
    c2ProgressBar_->setValue(100);
    c2ProgressBar_->setVisible(false);
    appendLog("C2: Write OK");

    int escIdx = ev.value("esc_index").toInt(-1);
    if (escIdx >= 0 && escIdx < escModel_->rowCount()) {
        escModel_->setEscStatus(escIdx, EscStatus::Ok);
    }

    // Advance batch if active
    if (c2WizardActive_) {
        int escNum = c2WizardCurrent_;
        if (c2WizardCurrent_ >= c2WizardTotal_) {
            // All done!
            c2WizardActive_ = false;
            c2CancelBatchBtn_->setVisible(false);
            c2WriteBtn_->setText("Erase + Write");
            c2WriteBtn_->setStyleSheet("QPushButton { background-color: #18181b; color: white; padding: 8px 16px; border-radius: 4px; } QPushButton:hover { background-color: #27272a; } QPushButton:disabled { opacity: 0.5; }");
            c2WriteBtn_->setEnabled(true);
            c2WizardStatusLabel_->setText(QString("✓ All %1 ESCs flashed!").arg(c2WizardTotal_));
            c2WizardStatusLabel_->setStyleSheet("color: #16a34a; padding: 8px;");
            appendLog(QString("C2 Batch: Completed! All %1 ESCs flashed").arg(c2WizardTotal_));
            showC2PostWriteNotice();
        } else {
            // Prompt for next ESC
            int nextEsc = c2WizardCurrent_ + 1;
            c2WriteBtn_->setText(QString("Flash ESC #%1").arg(nextEsc));
            c2WriteBtn_->setStyleSheet("QPushButton { background-color: #16a34a; color: white; padding: 8px 16px; border-radius: 4px; } QPushButton:hover { background-color: #15803d; } QPushButton:disabled { opacity: 0.5; }");
            c2WriteBtn_->setEnabled(true);
            c2WizardStatusLabel_->setText(QString("✓ ESC #%1 done! Connect ESC #%2, click 'Flash ESC #%2'\n\nProgress: %1/%3")
                .arg(escNum).arg(nextEsc).arg(c2WizardTotal_));
            c2WizardStatusLabel_->setStyleSheet("color: #16a34a; padding: 8px;");
        }
    } else {
        showC2PostWriteNotice();
    }
}

void MainWindow::handleC2WriteFail(const QJsonObject& ev) {
    QString error = ev.value("error").toString("Write failed");
    c2ProgressLabel_->setText("Write failed");
    c2ProgressBar_->setVisible(false);
    appendLog("C2: Write failed - " + error);

    // Handle batch failure - allow retry
    if (c2WizardActive_) {
        int escNum = c2WizardCurrent_;
        c2WriteBtn_->setText(QString("Retry ESC #%1").arg(escNum));
        c2WriteBtn_->setStyleSheet("QPushButton { background-color: #dc2626; color: white; padding: 8px 16px; border-radius: 4px; } QPushButton:hover { background-color: #b91c1c; } QPushButton:disabled { opacity: 0.5; }");
        c2WriteBtn_->setEnabled(true);
        c2WizardStatusLabel_->setText(QString("Write failed for ESC #%1: %2\n\nCheck connection and retry\n\nProgress: %3/%4")
            .arg(escNum).arg(error).arg(escNum - 1).arg(c2WizardTotal_));
        c2WizardStatusLabel_->setStyleSheet("color: #dc2626; padding: 8px;");
        c2WizardCurrent_--;  // Allow retry of same ESC
    } else {
        // Single mode - re-enable button
        c2WriteBtn_->setEnabled(true);
    }
}

void MainWindow::handleC2InstallOk(const QJsonObject& ev) {
    QString board = ev.value("board").toString();
    c2InterfaceStatusLabel_->setText(QString("✓ %1 firmware installed").arg(board.toUpper()));
    c2InterfaceStatusLabel_->setStyleSheet("color: #16a34a;");
    appendLog("C2: Interface firmware installed");
}

void MainWindow::handleC2InstallFail(const QJsonObject& ev) {
    QString error = ev.value("error").toString("Install failed");
    c2InterfaceStatusLabel_->setText("✗ Install failed");
    c2InterfaceStatusLabel_->setStyleSheet("color: #dc2626;");
    appendLog("C2: Install failed - " + error);
    showWarning("C2 install failed: " + error + "\n\nMake sure avrdude is installed and in your PATH.");
}

void MainWindow::handleC2InstallProgress(const QJsonObject& ev) {
    QString message = ev.value("message").toString();
    c2InterfaceStatusLabel_->setText(message);
}

// ========== Tab change: lock ESC sidebar on C2 tab ==========

void MainWindow::onTabChanged(int index) {
    bool isC2 = (tabWidget_->widget(index) == c2Tab_);

    if (isC2 && !escSelectionLocked_) {
        // Save current selection and lock sidebar
        savedEscSelection_ = selectedEscIndex_;
        escListView_->setSelectionMode(QAbstractItemView::NoSelection);
        escListView_->clearSelection();
        selectedEscIndex_ = -1;
        escSelectionLocked_ = true;
        updateSettingsPanel();
        updateUiState();
    } else if (!isC2 && escSelectionLocked_) {
        // Restore selection mode and previous selection
        escSelectionLocked_ = false;
        escListView_->setSelectionMode(QAbstractItemView::SingleSelection);
        if (savedEscSelection_ >= 0 && savedEscSelection_ < escModel_->rowCount()) {
            QModelIndex idx = escModel_->index(savedEscSelection_, 0);
            escListView_->setCurrentIndex(idx);
            selectedEscIndex_ = savedEscSelection_;
        }
        savedEscSelection_ = -1;
        updateSettingsPanel();
        updateUiState();
    }
}

// ========== C2 pre-write / post-write popups ==========

bool MainWindow::showC2PreWriteAdvice() {
    auto answer = QMessageBox::information(this, "Before you Erase + Write (C2)",
        "Keep a battery connected during erase/write.\n\n"
        "Tip (AIO quirk): If erase/write fails on one ESC, try flashing\n"
        "a different motor output first (often the diagonal one: 1\u21943,\n"
        "2\u21944), then retry the failing ESC. This can re-sync passthrough\n"
        "on some AIO/4-in-1 boards.",
        QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok);
    return (answer == QMessageBox::Ok);
}

void MainWindow::showC2PostWriteNotice() {
    QMessageBox::information(this, "C2 Write Complete",
        "Disconnect the battery (LiPo) from the AIO/ESC, wait ~3\u20135 seconds,\n"
        "then reconnect.\n\n"
        "Then run a normal Read (Read tab \u2192 Connect + Read).\n\n"
        "Without a power cycle, the next read may show stale/incorrect info.");
}

void MainWindow::showFlashPostWriteNotice() {
    QMessageBox::information(this, "Flash complete - power cycle required",
        "Disconnect the battery (LiPo) from the AIO/ESC, wait ~3\u20135 seconds,\n"
        "then reconnect.\n\n"
        "Then run a normal Read (Read tab \u2192 Connect + Read).\n\n"
        "Without a power cycle, the next read may show stale/incorrect info.");
}

}
