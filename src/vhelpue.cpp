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
        m_listWidget->addItem(tr("Esc/Ctrl+[: Hide Universal Entry"));
        m_listWidget->addItem(tr("Ctrl+U: Clear the command input"));
        m_listWidget->addItem(tr("Ctrl+E: Clear the command input except the entry key"));
        m_listWidget->addItem(tr("Ctrl+J: Select next item"));
        m_listWidget->addItem(tr("Ctrl+K: Select previous item"));
        m_listWidget->addItem(tr("Enter: Activate current item"));
        m_listWidget->addItem(tr("Ctrl+R: Select current item's parent item"));
        m_listWidget->addItem(tr("Ctrl+T: Expand/Collapse current item"));
        m_listWidget->addItem(tr("Ctrl+S: Sort the items"));

        return true;
    }

    return false;
}
