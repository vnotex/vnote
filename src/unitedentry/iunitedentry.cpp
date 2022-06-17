#include "iunitedentry.h"

#include <QWidget>
#include <QCoreApplication>
#include <QDebug>
#include <QKeyEvent>

#include <widgets/treewidget.h>

#include "unitedentrymgr.h"

using namespace vnotex;

IUnitedEntry::IUnitedEntry(const QString &p_name,
                           const QString &p_description,
                           UnitedEntryMgr *p_mgr,
                           QObject *p_parent)
    : QObject(p_parent),
      m_mgr(p_mgr),
      m_name(p_name),
      m_description(p_description)
{
}

const QString &IUnitedEntry::name() const
{
    return m_name;
}

QString IUnitedEntry::description() const
{
    return m_description;
}

void IUnitedEntry::process(const QString &p_args,
                           const std::function<void(const QSharedPointer<QWidget> &)> &p_popupWidgetFunc)
{
    if (!m_initialized) {
        m_initialized = true;

        initOnFirstProcess();
    }

    m_askedToStop.store(0);

    return processInternal(p_args, p_popupWidgetFunc);
}

void IUnitedEntry::stop()
{
    m_askedToStop.store(1);
}

bool IUnitedEntry::isAskedToStop() const
{
    return m_askedToStop.load() == 1;
}

void IUnitedEntry::setOngoing(bool p_ongoing)
{
    m_ongoing = p_ongoing;
}

bool IUnitedEntry::isOngoing() const
{
    return m_ongoing;
}

void IUnitedEntry::handleAction(Action p_act)
{
    auto widget = currentPopupWidget();
    if (!widget) {
        return;
    }
    handleActionCommon(p_act, widget.data());
}

void IUnitedEntry::handleActionCommon(Action p_act, QWidget *p_widget)
{
    switch (p_act)
    {
    case Action::NextItem:
    {
        auto eve = new QKeyEvent(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
        QCoreApplication::postEvent(p_widget, eve);
        break;
    }

    case Action::PreviousItem:
    {
        auto eve = new QKeyEvent(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
        QCoreApplication::postEvent(p_widget, eve);
        break;
    }

    case Action::Activate:
    {
        auto eve = new QKeyEvent(QEvent::KeyPress, Qt::Key_Enter, Qt::NoModifier);
        QCoreApplication::postEvent(p_widget, eve);
        break;
    }

    case Action::LevelUp:
    {
        auto treeWidget = dynamic_cast<QTreeWidget *>(p_widget);
        if (treeWidget) {
            TreeWidget::selectParentItem(treeWidget);
        }
        break;
    }

    case Action::ExpandCollapse:
    {
        auto treeWidget = dynamic_cast<QTreeWidget *>(p_widget);
        if (treeWidget) {
            auto item = treeWidget->currentItem();
            if (item) {
                item->setExpanded(!item->isExpanded());
            }
        }
        break;
    }

    case Action::ExpandCollapseAll:
    {
        auto treeWidget = dynamic_cast<QTreeWidget *>(p_widget);
        if (treeWidget) {
            if (TreeWidget::isExpanded(treeWidget)) {
                treeWidget->collapseAll();
            } else {
                treeWidget->expandAll();
            }
        }
        break;
    }

    default:
        Q_ASSERT(false);
        break;
    }
}

bool IUnitedEntry::isAliasOf(const IUnitedEntry *p_entry) const
{
    Q_UNUSED(p_entry);
    return false;
}
