#include "helpunitedentry.h"

#include <widgets/treewidget.h>

#include "entrywidgetfactory.h"

using namespace vnotex;

HelpUnitedEntry::HelpUnitedEntry(UnitedEntryMgr *p_mgr, QObject *p_parent)
    : IUnitedEntry("help",
                   tr("Help information about United Entry"),
                   p_mgr,
                   p_parent)
{
}

QSharedPointer<QWidget> HelpUnitedEntry::currentPopupWidget() const
{
    return m_infoTree;
}

void HelpUnitedEntry::initOnFirstProcess()
{
    m_infoTree = EntryWidgetFactory::createTreeWidget(2);
    m_infoTree->setHeaderHidden(false);
    m_infoTree->setHeaderLabels(QStringList() << tr("Shortcut") << tr("Description"));

    QVector<QStringList> shortcuts = {{"Esc/Ctrl+[", tr("Close United Entry")},
                                      {"Up/Ctrl+K", tr("Go to previous item")},
                                      {"Down/Ctrl+J", tr("Go to next item")},
                                      {"Ctrl+L", tr("Go to the item one level up")},
                                      {"Ctrl+I", tr("Expand/Collapse current item")},
                                      {"Ctrl+B", tr("Expand/Collapse all the items")},
                                      {"Enter", tr("Activate current item")},
                                      {"Ctrl+E", tr("Clear the input except the entry name")},
                                      {"Ctrl+F", tr("Select the entry name")},
                                      {"Ctrl+D", tr("Stop current entry")}};
    for (const auto &shortcut : shortcuts) {
        m_infoTree->addTopLevelItem(new QTreeWidgetItem(shortcut));
    }
}

void HelpUnitedEntry::processInternal(const QString &p_args,
                                      const std::function<void(const QSharedPointer<QWidget> &)> &p_popupWidgetFunc)
{
    Q_UNUSED(p_args);
    p_popupWidgetFunc(m_infoTree);
}
