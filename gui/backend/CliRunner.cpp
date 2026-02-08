#include "CliRunner.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace gui {

CliRunner::CliRunner(QObject* parent)
    : QObject(parent)
{
    QString appDir = QCoreApplication::applicationDirPath();
    QString sameDirCli = QDir(appDir).filePath("prober.exe");
    QString distCli = QDir(appDir).filePath("../../prober.exe");
    if (QFileInfo::exists(sameDirCli)) {
        cliPath_ = sameDirCli;
    } else if (QFileInfo::exists(distCli)) {
        cliPath_ = QFileInfo(distCli).canonicalFilePath();
    } else {
        cliPath_ = sameDirCli;  // fallback, will show error later
    }
}

CliRunner::~CliRunner() {
    if (process_ && process_->state() != QProcess::NotRunning) {
        killProcessTree();
        process_->waitForFinished(1000);
    }
}

void CliRunner::setCliPath(const QString& path) {
    cliPath_ = path;
}

QList<PortInfo> CliRunner::listPortsSync() {
    QList<PortInfo> result;

    if (!QFileInfo::exists(cliPath_)) {
        qWarning() << "CLI not found at:" << cliPath_;
        return result;
    }

    QProcess proc;
    proc.start(cliPath_, {"--list-ports", "--json"});
    if (!proc.waitForStarted(3000)) {
        qWarning() << "Failed to start CLI:" << proc.errorString();
        return result;
    }
    if (!proc.waitForFinished(5000)) {
        qWarning() << "CLI timed out";
        return result;
    }
    if (proc.exitCode() != 0) {
        qWarning() << "CLI exited with code:" << proc.exitCode() << proc.readAllStandardError();
        return result;
    }
    QByteArray out = proc.readAllStandardOutput();
    QJsonDocument doc = QJsonDocument::fromJson(out);
    if (!doc.isArray()) {
        qWarning() << "Invalid JSON from CLI:" << out;
        return result;
    }

    for (const auto& v : doc.array()) {
        if (!v.isObject()) continue;
        QJsonObject obj = v.toObject();
        PortInfo pi;
        pi.port = obj.value("port").toString();
        pi.description = obj.value("description").toString();
        pi.hwid = obj.value("hwid").toString();
        result.append(pi);
    }
    return result;
}

bool CliRunner::startProcess(const QStringList& args, OpKind op) {
    if (isBusy()) return false;

    process_ = std::make_unique<QProcess>(this);
    currentOp_ = op;
    cancelRequested_ = false;
    stdoutBuffer_.clear();
    stderrBuffer_.clear();

    connect(process_.get(), &QProcess::readyReadStandardOutput, this, &CliRunner::onReadyReadStdout);
    connect(process_.get(), &QProcess::readyReadStandardError, this, &CliRunner::onReadyReadStderr);
    connect(process_.get(), QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &CliRunner::onProcessFinished);

    process_->start(cliPath_, args);
    return process_->waitForStarted(3000);
}

bool CliRunner::startReadAll(const ConnectionOptions& conn, const ReadOptions& opts) {
    QStringList args;
    args << "--ui-json"
         << "--port" << conn.port
         << "--baud" << QString::number(conn.baud)
         << "--read" << "--all"
         << "--settle-ms" << QString::number(conn.settleMs)
         << "--read-rounds" << QString::number(opts.readRounds)
         << "--read-round-sleep-ms" << QString::number(opts.readRoundSleepMs);
    if (conn.trace) args << "--trace";
    return startProcess(args, OpKind::Read);
}

bool CliRunner::startReadOne(const ConnectionOptions& conn, int escIndex1Based) {
    QStringList args;
    args << "--ui-json"
         << "--port" << conn.port
         << "--baud" << QString::number(conn.baud)
         << "--read"
         << "--index" << QString::number(escIndex1Based)
         << "--settle-ms" << QString::number(conn.settleMs);
    if (conn.trace) args << "--trace";
    return startProcess(args, OpKind::Read);
}

bool CliRunner::startWriteSettings(const ConnectionOptions& conn, int escIndex1Based,
                                    const QList<QPair<QString, QVariant>>& sets) {
    QStringList args;
    args << "--ui-json"
         << "--port" << conn.port
         << "--baud" << QString::number(conn.baud)
         << "--write-settings"
         << "--index" << QString::number(escIndex1Based)
         << "--settle-ms" << QString::number(conn.settleMs);
    if (conn.trace) args << "--trace";

    for (const auto& kv : sets) {
        args << "--set" << QString("%1=%2").arg(kv.first, kv.second.toString());
    }
    return startProcess(args, OpKind::WriteSettings);
}

bool CliRunner::startWriteSettingsAll(const ConnectionOptions& conn,
                                       const QList<QPair<QString, QVariant>>& sets) {
    QStringList args;
    args << "--ui-json"
         << "--port" << conn.port
         << "--baud" << QString::number(conn.baud)
         << "--write-settings"
         << "--all"
         << "--settle-ms" << QString::number(conn.settleMs);
    if (conn.trace) args << "--trace";

    for (const auto& kv : sets) {
        args << "--set" << QString("%1=%2").arg(kv.first, kv.second.toString());
    }
    return startProcess(args, OpKind::WriteSettings);
}

bool CliRunner::startFlash(const ConnectionOptions& conn, const FlashOptions& opts) {
    QStringList args;
    args << "--ui-json"
         << "--port" << conn.port
         << "--baud" << QString::number(conn.baud)
         << "--hex" << opts.hexPath
         << "--settle-ms" << QString::number(conn.settleMs)
         << "--verify" << opts.verify;

    if (opts.targetAll) {
        args << "--all";
    } else {
        args << "--index" << QString::number(opts.targetIndex);
    }

    if (opts.eraseEeprom) args << "--erase-eeprom";
    if (opts.fullEraseApp) args << "--full-erase-app";
    if (opts.fullEraseEntireApp) args << "--full-erase-entire-app";
    if (opts.verifyAllBytes) args << "--verify-all-bytes";
    if (opts.dryRun) args << "--dry-run";
    if (opts.skipMissing) args << "--skip-missing";
    if (opts.slowSwitching) {
        args << "--flash-preselect-tries" << "4"
             << "--flash-preselect-sleep-ms" << "200";
    }
    // Always pass stability parameters from GUI
    args << "--flash-inter-esc-ms" << QString::number(opts.slowSwitching ? std::max(opts.interEscMs, 800) : opts.interEscMs);
    args << "--flash-post-select-ms" << QString::number(opts.postSelectMs);
    args << "--flash-erase-retries" << QString::number(opts.eraseRetries);
    args << "--flash-write-retries" << QString::number(opts.writeRetries);
    args << "--verify-read-retries" << QString::number(opts.verifyReadRetries);
    if (!opts.assumeSig.trimmed().isEmpty()) {
        args << "--assume-sig" << opts.assumeSig.trimmed();
    }
    if (conn.trace) args << "--trace";

    return startProcess(args, OpKind::Flash);
}

bool CliRunner::startC2Detect(const QString& port, int timeoutMs, int connectDelayMs) {
    QStringList args;
    args << "--ui-json"
         << "--c2"
         << "--c2-port" << port
         << "--c2-detect"
         << "--c2-timeout-ms" << QString::number(timeoutMs)
         << "--c2-connect-delay-ms" << QString::number(connectDelayMs);
    return startProcess(args, OpKind::C2Detect);
}

bool CliRunner::startC2ReadInfo(const QString& port, int timeoutMs, int connectDelayMs) {
    QStringList args;
    args << "--ui-json"
         << "--c2"
         << "--c2-port" << port
         << "--c2-read-info"
         << "--c2-timeout-ms" << QString::number(timeoutMs)
         << "--c2-connect-delay-ms" << QString::number(connectDelayMs);
    return startProcess(args, OpKind::C2ReadInfo);
}

bool CliRunner::startC2Erase(const QString& port, int timeoutMs, int connectDelayMs) {
    QStringList args;
    args << "--ui-json"
         << "--c2"
         << "--c2-port" << port
         << "--c2-erase"
         << "--c2-timeout-ms" << QString::number(timeoutMs)
         << "--c2-connect-delay-ms" << QString::number(connectDelayMs);
    return startProcess(args, OpKind::C2Erase);
}

bool CliRunner::startC2WriteHex(const QString& port, const QString& hexPath, int escIndex, int timeoutMs, int connectDelayMs) {
    QStringList args;
    args << "--ui-json"
         << "--c2"
         << "--c2-port" << port
         << "--c2-write-hex" << hexPath
         << "--c2-timeout-ms" << QString::number(timeoutMs)
         << "--c2-connect-delay-ms" << QString::number(connectDelayMs);
    if (escIndex >= 0) {
        args << "--ui-esc-index" << QString::number(escIndex);
    }
    return startProcess(args, OpKind::C2Write);
}

bool CliRunner::startC2Install(const QString& port, const QString& board) {
    QStringList args;
    args << "--ui-json"
         << "--c2"
         << "--c2-port" << port
         << "--c2-install" << board;
    return startProcess(args, OpKind::C2Install);
}

void CliRunner::cancel() {
    if (!process_ || process_->state() == QProcess::NotRunning) return;
    cancelRequested_ = true;
    killProcessTree();
}

void CliRunner::killProcessTree() {
    if (!process_) return;

#ifdef _WIN32
    qint64 pid = process_->processId();
    if (pid > 0) {
        QProcess::startDetached("taskkill", {"/PID", QString::number(pid), "/T", "/F"});
    }
#else
    process_->kill();
#endif
}

void CliRunner::onReadyReadStdout() {
    if (!process_) return;
    stdoutBuffer_ += QString::fromUtf8(process_->readAllStandardOutput());
    QStringList lines = stdoutBuffer_.split('\n');
    stdoutBuffer_ = lines.takeLast();
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            processLine(trimmed, "stdout");
        }
    }
}

void CliRunner::onReadyReadStderr() {
    if (!process_) return;
    stderrBuffer_ += QString::fromUtf8(process_->readAllStandardError());
    QStringList lines = stderrBuffer_.split('\n');
    stderrBuffer_ = lines.takeLast();
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            processLine(trimmed, "stderr");
        }
    }
}

void CliRunner::processLine(const QString& line, const QString& stream) {
    QJsonObject obj;
    if (tryParseNdjson(line, obj)) {
        emit uiEvent(obj);
    } else {
        if (line.startsWith('{')) {
            emit logLine(stream, QString("[ndjson-parse-fail] %1").arg(line));
        } else {
            emit logLine(stream, line);
        }
    }
}

bool CliRunner::tryParseNdjson(const QString& line, QJsonObject& out) {
    if (!line.startsWith('{')) return false;
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) return false;
    if (!doc.isObject()) return false;
    QJsonObject obj = doc.object();
    if (!obj.contains("type") || !obj.value("type").isString()) return false;
    out = obj;
    return true;
}

void CliRunner::onProcessFinished(int exitCode, QProcess::ExitStatus /*status*/) {
    OpKind op = currentOp_;
    currentOp_ = OpKind::None;
    bool wasCancelled = cancelRequested_;
    cancelRequested_ = false;
    process_.reset();
    emit processExited(exitCode, wasCancelled);
}

}
