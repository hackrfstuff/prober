#include "EscListModel.h"

namespace gui {

EscListModel::EscListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int EscListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return escs_.size();
}

QVariant EscListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= escs_.size())
        return QVariant();

    const EscState& esc = escs_.at(index.row());

    switch (role) {
        case Qt::DisplayRole:
        case DisplayNameRole:
            return QString("ESC%1").arg(esc.index + 1);
        case IndexRole:
            return esc.index;
        case StatusRole:
            return static_cast<int>(esc.status);
        case StatusTextRole:
            return escStatusToString(esc.status);
        case ReachableRole:
            if (!esc.reachable.has_value()) return QVariant();
            return esc.reachable.value();
        case ErrorRole:
            return esc.error;
        case HasSettingsRole:
            return esc.hasSettings();
        case TargetRole:
            return esc.identity.has_value() ? esc.identity->target : QString();
        case FirmwareRole:
            return esc.identity.has_value() ? esc.identity->fw : QString();
    }
    return QVariant();
}

QHash<int, QByteArray> EscListModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[IndexRole] = "escIndex";
    roles[StatusRole] = "status";
    roles[StatusTextRole] = "statusText";
    roles[ReachableRole] = "reachable";
    roles[ErrorRole] = "error";
    roles[HasSettingsRole] = "hasSettings";
    roles[TargetRole] = "target";
    roles[FirmwareRole] = "firmware";
    roles[DisplayNameRole] = "displayName";
    return roles;
}

void EscListModel::clear() {
    if (escs_.isEmpty()) return;
    beginResetModel();
    escs_.clear();
    endResetModel();
}

void EscListModel::seedEscs(int count) {
    beginResetModel();
    escs_.clear();
    for (int i = 0; i < count; ++i) {
        EscState esc;
        esc.index = i;
        esc.status = EscStatus::Idle;
        escs_.append(esc);
    }
    endResetModel();
}

void EscListModel::updateEsc(int index0, const EscState& state) {
    if (index0 < 0) return;
    while (escs_.size() <= index0) {
        beginInsertRows(QModelIndex(), escs_.size(), escs_.size());
        EscState esc;
        esc.index = escs_.size();
        escs_.append(esc);
        endInsertRows();
    }
    escs_[index0] = state;
    QModelIndex idx = createIndex(index0, 0);
    emit dataChanged(idx, idx);
    emit escUpdated(index0);
}

void EscListModel::setEscStatus(int index0, EscStatus status, const QString& error) {
    if (index0 < 0 || index0 >= escs_.size()) return;
    escs_[index0].status = status;
    escs_[index0].error = error;
    QModelIndex idx = createIndex(index0, 0);
    emit dataChanged(idx, idx);
    emit escUpdated(index0);
}

void EscListModel::setEscReachable(int index0, std::optional<bool> reachable) {
    if (index0 < 0 || index0 >= escs_.size()) return;
    escs_[index0].reachable = reachable;
    QModelIndex idx = createIndex(index0, 0);
    emit dataChanged(idx, idx);
}

void EscListModel::setEscSettings(int index0, const QMap<QString, int>& settings,
                                   const QMap<QString, int>& baseSettings) {
    if (index0 < 0 || index0 >= escs_.size()) return;
    escs_[index0].settings = settings;
    escs_[index0].baseSettings = baseSettings;
    QModelIndex idx = createIndex(index0, 0);
    emit dataChanged(idx, idx);
    emit escUpdated(index0);
}

void EscListModel::setEscIdentity(int index0, const QString& sig, const EscIdentity& identity) {
    if (index0 < 0 || index0 >= escs_.size()) return;
    escs_[index0].sig = sig;
    escs_[index0].identity = identity;
    QModelIndex idx = createIndex(index0, 0);
    emit dataChanged(idx, idx);
}

EscState EscListModel::escAt(int index0) const {
    if (index0 < 0 || index0 >= escs_.size()) return EscState();
    return escs_.at(index0);
}

QList<int> EscListModel::indicesWithBaseSettings() const {
    QList<int> result;
    for (int i = 0; i < escs_.size(); ++i) {
        if (escs_[i].hasBaseSettings()) {
            result.append(i);
        }
    }
    return result;
}

void EscListModel::setAllStatus(EscStatus status, const QString& error) {
    for (int i = 0; i < escs_.size(); ++i) {
        escs_[i].status = status;
        escs_[i].error = error;
    }
    if (!escs_.isEmpty()) {
        emit dataChanged(createIndex(0, 0), createIndex(escs_.size() - 1, 0));
    }
}

void EscListModel::finalizeQueuedOrFlashing(const QString& reason) {
    for (int i = 0; i < escs_.size(); ++i) {
        if (escs_[i].status == EscStatus::Queued || escs_[i].status == EscStatus::Flashing) {
            escs_[i].status = EscStatus::Fail;
            escs_[i].error = reason;
        }
    }
    if (!escs_.isEmpty()) {
        emit dataChanged(createIndex(0, 0), createIndex(escs_.size() - 1, 0));
    }
}

}
