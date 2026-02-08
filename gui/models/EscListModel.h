#pragma once

#include "../types.h"
#include <QAbstractListModel>
#include <QList>

namespace gui {

class EscListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        IndexRole = Qt::UserRole + 1,
        StatusRole,
        StatusTextRole,
        ReachableRole,
        ErrorRole,
        HasSettingsRole,
        TargetRole,
        FirmwareRole,
        DisplayNameRole
    };

    explicit EscListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void clear();
    void seedEscs(int count);
    void updateEsc(int index0, const EscState& state);
    void setEscStatus(int index0, EscStatus status, const QString& error = QString());
    void setEscReachable(int index0, std::optional<bool> reachable);
    void setEscSettings(int index0, const QMap<QString, int>& settings, const QMap<QString, int>& baseSettings);
    void setEscIdentity(int index0, const QString& sig, const EscIdentity& identity);

    EscState escAt(int index0) const;
    int escCount() const { return escs_.size(); }
    QList<int> indicesWithBaseSettings() const;

    void setAllStatus(EscStatus status, const QString& error = QString());
    void finalizeQueuedOrFlashing(const QString& reason);

signals:
    void escUpdated(int index0);

private:
    QList<EscState> escs_;
};

}
