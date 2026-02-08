#pragma once

#include "../types.h"
#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <QTimer>
#include <memory>

namespace gui {

class CliRunner : public QObject {
    Q_OBJECT

public:
    explicit CliRunner(QObject* parent = nullptr);
    ~CliRunner();

    void setCliPath(const QString& path);
    QString cliPath() const { return cliPath_; }

    QList<PortInfo> listPortsSync();

    bool startReadAll(const ConnectionOptions& conn, const ReadOptions& opts);
    bool startReadOne(const ConnectionOptions& conn, int escIndex1Based);
    bool startWriteSettings(const ConnectionOptions& conn, int escIndex1Based, const QList<QPair<QString, QVariant>>& sets);
    bool startWriteSettingsAll(const ConnectionOptions& conn, const QList<QPair<QString, QVariant>>& sets);
    bool startFlash(const ConnectionOptions& conn, const FlashOptions& opts);

    // C2 operations
    bool startC2Detect(const QString& port, int timeoutMs = 2000, int connectDelayMs = 2000);
    bool startC2ReadInfo(const QString& port, int timeoutMs = 2000, int connectDelayMs = 2000);
    bool startC2Erase(const QString& port, int timeoutMs = 2000, int connectDelayMs = 2000);
    bool startC2WriteHex(const QString& port, const QString& hexPath, int escIndex = -1, int timeoutMs = 2000, int connectDelayMs = 2000);
    bool startC2Install(const QString& port, const QString& board);

    void cancel();

    bool isBusy() const { return process_ != nullptr && process_->state() != QProcess::NotRunning; }
    OpKind currentOp() const { return currentOp_; }

signals:
    void uiEvent(const QJsonObject& event);
    void logLine(const QString& stream, const QString& text);
    void processExited(int code, bool cancelled);

private slots:
    void onReadyReadStdout();
    void onReadyReadStderr();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    void processLine(const QString& line, const QString& stream);
    bool tryParseNdjson(const QString& line, QJsonObject& out);
    bool startProcess(const QStringList& args, OpKind op);
    void killProcessTree();

    QString cliPath_;
    std::unique_ptr<QProcess> process_;
    OpKind currentOp_ = OpKind::None;
    bool cancelRequested_ = false;

    QString stdoutBuffer_;
    QString stderrBuffer_;
};

}
