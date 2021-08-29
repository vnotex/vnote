#include "notemanagementpage.h"

#include <QCheckBox>
#include <QFormLayout>

#include <widgets/widgetsfactory.h>
#include <core/coreconfig.h>
#include <core/configmgr.h>
#include <utils/widgetutils.h>
#include <core/vnotex.h>

using namespace vnotex;

NoteManagementPage::NoteManagementPage(QWidget *p_parent)
    : SettingsPage(p_parent)
{
    setupUI();
}

void NoteManagementPage::setupUI()
{
    auto mainLayout = WidgetsFactory::createFormLayout(this);

    {
        const QString label(tr("Per-Notebook access history"));
        m_perNotebookHistoryCheckBox = WidgetsFactory::createCheckBox(label, this);
        m_perNotebookHistoryCheckBox->setToolTip(tr("Store note access history in its notebook"));
        mainLayout->addRow(m_perNotebookHistoryCheckBox);
        addSearchItem(label, m_perNotebookHistoryCheckBox->toolTip(), m_perNotebookHistoryCheckBox);
        connect(m_perNotebookHistoryCheckBox, &QCheckBox::stateChanged,
                this, &NoteManagementPage::pageIsChanged);
    }
}

void NoteManagementPage::loadInternal()
{
    const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

    m_perNotebookHistoryCheckBox->setChecked(coreConfig.isPerNotebookHistoryEnabled());
}

bool NoteManagementPage::saveInternal()
{
    auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

    coreConfig.setPerNotebookHistoryEnabled(m_perNotebookHistoryCheckBox->isChecked());

    return true;
}

QString NoteManagementPage::title() const
{
    return tr("Note Management");
}
