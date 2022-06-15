#include "unitedentryalias.h"

#include <QDebug>
#include <QLabel>

#include "unitedentrymgr.h"
#include "entrywidgetfactory.h"

using namespace vnotex;

UnitedEntryAlias::UnitedEntryAlias(const QString &p_name,
                                   const QString &p_description,
                                   const QString &p_value,
                                   UnitedEntryMgr *p_mgr,
                                   QObject *p_parent)
    : IUnitedEntry(p_name, p_description, p_mgr, p_parent),
      m_value(p_value)
{
    m_alias = UnitedEntryHelper::parseUserEntry(m_value);
}

UnitedEntryAlias::UnitedEntryAlias(const QJsonObject &p_obj,
                                   UnitedEntryMgr *p_mgr,
                                   QObject *p_parent)
    : UnitedEntryAlias(p_obj[QStringLiteral("name")].toString(),
                       p_obj[QStringLiteral("description")].toString(),
                       p_obj[QStringLiteral("value")].toString(),
                       p_mgr,
                       p_parent)
{
}

QString UnitedEntryAlias::description() const
{
    return tr("[Alias] ") + IUnitedEntry::description();
}

QJsonObject UnitedEntryAlias::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("name")] = name();
    obj[QStringLiteral("description")] = description();
    obj[QStringLiteral("value")] = m_value;
    return obj;
}

void UnitedEntryAlias::initOnFirstProcess()
{
    m_realEntry = m_mgr->findEntry(m_alias.m_name).data();
    if (!m_realEntry) {
        qWarning() << "invalid UnitedEntry alias" << name() << m_value;
    } else {
        connect(m_realEntry, &IUnitedEntry::finished,
                this, &IUnitedEntry::finished);
        connect(m_realEntry, &IUnitedEntry::itemActivated,
                this, &IUnitedEntry::itemActivated);
    }
}

void UnitedEntryAlias::processInternal(const QString &p_args,
                                       const std::function<void(const QSharedPointer<QWidget> &)> &p_popupWidgetFunc)
{
    if (!m_realEntry) {
        auto label = EntryWidgetFactory::createLabel(tr("Invalid United Entry alias: %1").arg(m_value));
        p_popupWidgetFunc(label);
        emit finished();
        return;
    }

    m_realEntry->process(m_alias.m_args + " " + p_args, p_popupWidgetFunc);
}

bool UnitedEntryAlias::isOngoing() const
{
    if (m_realEntry) {
        return m_realEntry->isOngoing();
    }
    return false;
}

void UnitedEntryAlias::setOngoing(bool p_ongoing)
{
    Q_UNUSED(p_ongoing);
    Q_ASSERT(false);
}

void UnitedEntryAlias::handleAction(Action p_act)
{
    if (m_realEntry) {
        m_realEntry->handleAction(p_act);
    }
}

QSharedPointer<QWidget> UnitedEntryAlias::currentPopupWidget() const
{
    if (m_realEntry) {
        return m_realEntry->currentPopupWidget();
    }

    return nullptr;
}

bool UnitedEntryAlias::isAliasOf(const IUnitedEntry *p_entry) const
{
    return p_entry == m_realEntry;
}
