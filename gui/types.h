#pragma once

#include <QString>
#include <QMap>
#include <QVariant>
#include <optional>

namespace gui {

struct PortInfo {
    QString port;
    QString description;
    QString hwid;
};

enum class EscStatus {
    Idle,
    Reading,
    Writing,
    Flashing,
    Queued,
    Ok,
    Unstable,
    Fail
};

inline QString escStatusToString(EscStatus s) {
    switch (s) {
        case EscStatus::Idle: return "Idle";
        case EscStatus::Reading: return "Reading";
        case EscStatus::Writing: return "Writing";
        case EscStatus::Flashing: return "Flashing";
        case EscStatus::Queued: return "Queued";
        case EscStatus::Ok: return "OK";
        case EscStatus::Unstable: return "Unstable";
        case EscStatus::Fail: return "Fail";
    }
    return "Unknown";
}

struct EscIdentity {
    int layoutVersion = 0;
    QString fw;
    QString target;
    int pwmKhz = 0;
};

struct EscState {
    int index = 0;
    EscStatus status = EscStatus::Idle;
    std::optional<bool> reachable;
    QString error;
    QString sig;
    std::optional<EscIdentity> identity;
    QMap<QString, int> settings;
    QMap<QString, int> baseSettings;

    bool hasSettings() const { return !settings.isEmpty(); }
    bool hasBaseSettings() const { return !baseSettings.isEmpty(); }
};

enum class OpKind {
    None,
    Read,
    WriteSettings,
    Flash,
    // C2 operations
    C2Detect,
    C2ReadInfo,
    C2Erase,
    C2Write,
    C2Install
};

struct FlashOptions {
    QString hexPath;
    bool targetAll = false;
    int targetIndex = -1;
    QString verify = "fast";
    bool eraseEeprom = false;
    bool fullEraseApp = false;
    bool fullEraseEntireApp = false;
    bool verifyAllBytes = false;
    bool dryRun = false;
    bool skipMissing = true;
    bool slowSwitching = false;
    QString assumeSig;
    int eraseRetries = 3;
    int writeRetries = 3;
    int interEscMs = 250;
    int postSelectMs = 200;
    int eraseInterPageMs = 50;
    int writeInterBlockMs = 10;
    int verifyReadRetries = 3;
};

struct WriteOptions {
    int escIndex = -1;
    QList<QPair<QString, QVariant>> sets;
};

struct ReadOptions {
    bool all = true;
    int escIndex = -1;
    int readRounds = 3;
    int readRoundSleepMs = 150;
};

struct ConnectionOptions {
    QString port;
    int baud = 115200;
    int settleMs = 0;
    bool trace = false;
};

}
