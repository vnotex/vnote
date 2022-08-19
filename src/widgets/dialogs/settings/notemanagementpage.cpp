#include "notemanagementpage.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QComboBox>

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

    {
        m_lineEndingComboBox = WidgetsFactory::createComboBox(this);
        m_lineEndingComboBox->setToolTip(tr("Line ending used to write configuration files"));

        m_lineEndingComboBox->addItem(tr("Follow Platform"), (int)LineEndingPolicy::Platform);
        m_lineEndingComboBox->addItem(tr("LF (Linux/macOS)"), (int)LineEndingPolicy::LF);
        m_lineEndingComboBox->addItem(tr("CR LF (Windows)"), (int)LineEndingPolicy::CRLF);
        m_lineEndingComboBox->addItem(tr("CR"), (int)LineEndingPolicy::CR);

        const QString label(tr("Line ending:"));
        mainLayout->addRow(label, m_lineEndingComboBox);
        addSearchItem(label, m_lineEndingComboBox->toolTip(), m_lineEndingComboBox);
        connect(m_lineEndingComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &NoteManagementPage::pageIsChanged);
    }

    {
        m_defaultOpenModeComboBox = WidgetsFactory::createComboBox(this);
        m_defaultOpenModeComboBox->setToolTip(tr("Default mode when opening notes"));

        m_defaultOpenModeComboBox->addItem(tr("Read"), (int)ViewWindowMode::Read);
        m_defaultOpenModeComboBox->addItem(tr("Edit"), (int)ViewWindowMode::Edit);

        const QString label(tr("Default open mode:"));
        mainLayout->addRow(label, m_defaultOpenModeComboBox);
        addSearchItem(label, m_defaultOpenModeComboBox->toolTip(), m_defaultOpenModeComboBox);
        connect(m_defaultOpenModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &NoteManagementPage::pageIsChanged);
    }
}

void NoteManagementPage::loadInternal()
{
    const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

    m_perNotebookHistoryCheckBox->setChecked(coreConfig.isPerNotebookHistoryEnabled());

    {
        int idx = m_lineEndingComboBox->findData(static_cast<int>(coreConfig.getLineEndingPolicy()));
        if (idx == -1) {
            idx = 0;
        }
        m_lineEndingComboBox->setCurrentIndex(idx);
    }

    {
        int idx = m_defaultOpenModeComboBox->findData(static_cast<int>(coreConfig.getDefaultOpenMode()));
        if (idx == -1) {
            idx = 0;
        }
        m_defaultOpenModeComboBox->setCurrentIndex(idx);
    }
}

bool NoteManagementPage::saveInternal()
{
    auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

    coreConfig.setPerNotebookHistoryEnabled(m_perNotebookHistoryCheckBox->isChecked());

    {
        auto ending = m_lineEndingComboBox->currentData().toInt();
        coreConfig.setLineEndingPolicy(static_cast<LineEndingPolicy>(ending));
    }

    {
        auto mode = m_defaultOpenModeComboBox->currentData().toInt();
        coreConfig.setDefaultOpenMode(static_cast<ViewWindowMode>(mode));
    }

    return true;
}

QString NoteManagementPage::title() const
{
    return tr("Note Management");
}
