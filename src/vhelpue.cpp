#include "vhelpue.h"

#include "vlistwidget.h"


VHelpUE::VHelpUE(QObject *p_parent)
    : IUniversalEntry(p_parent),
      m_listWidget(NULL)
{
}

void VHelpUE::init()
{
    if (m_initialized) {
        return;
    }

    m_initialized = true;

    m_listWidget = new VListWidget(m_widgetParent);
    m_listWidget->setFitContent(true);
}

QString VHelpUE::description(int p_id) const
{
    Q_UNUSED(p_id);
    return tr("View help information about Universal Entry");
}

QWidget *VHelpUE::widget(int p_id)
{
    Q_UNUSED(p_id);

    init();

    return m_listWidget;
}

void VHelpUE::processCommand(int p_id, const QString &p_cmd)
{
    Q_UNUSED(p_id);
    Q_UNUSED(p_cmd);

    init();

    if (initListWidget()) {
        m_listWidget->updateGeometry();
        emit widgetUpdated();
    }

    emit stateUpdated(State::Success);
}

void VHelpUE::clear(int p_id)
{
    Q_UNUSED(p_id);
    m_listWidget->clearAll();
}

bool VHelpUE::initListWidget()
{
    if (m_listWidget->count() == 0) {
        m_listWidget->addItem(tr("Esc or Ctrl+[: Hide Universal Entry"));
        m_listWidget->addItem(tr("Ctrl+U: Clear the command input"));
        m_listWidget->addItem(tr("Ctrl+E: Clear the command input except the entry key"));
        m_listWidget->addItem(tr("Ctrl+F: Select the entry key to change"));
        m_listWidget->addItem(tr("Ctrl+D: Cancel the command"));
        m_listWidget->addItem(tr("Ctrl+J: Go to next item"));
        m_listWidget->addItem(tr("Ctrl+K: Go to previous item"));
        m_listWidget->addItem(tr("Ctrl+L: Go to current item's parent item"));
        m_listWidget->addItem(tr("Ctrl+I: Expand/Collapse current item"));
        m_listWidget->addItem(tr("Ctrl+S: Sort items"));
        m_listWidget->addItem(tr("Enter: Activate current item"));
        m_listWidget->addItem(tr("Ctrl+M: Browse current item folder or the folder containing current item"));
        m_listWidget->addItem(tr("Magic Switches:"));
        m_listWidget->addItem(tr("\\c or \\C: Case insensitive or sensitive"));
        m_listWidget->addItem(tr("\\r or \\R: Disable or enable regular expression"));
        m_listWidget->addItem(tr("\\f or \\F: Disable or enable fuzzy search"));
        m_listWidget->addItem(tr("\\w or \\W: Disable or enable whole word only"));

        return true;
    }

    return false;
}
