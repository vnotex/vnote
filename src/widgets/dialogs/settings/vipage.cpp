#include "vipage.h"

#include <QCheckBox>
#include <QFormLayout>

#include <vtextedit/viconfig.h>

#include <widgets/widgetsfactory.h>
#include <core/editorconfig.h>
#include <core/configmgr.h>
#include <utils/widgetutils.h>

#include "editorpage.h"

using namespace vnotex;

ViPage::ViPage(QWidget *p_parent)
    : SettingsPage(p_parent)
{
    setupUI();
}

void ViPage::setupUI()
{
    auto mainLayout = WidgetsFactory::createFormLayout(this);

    {
        const QString label(tr("Ctrl+C/X to copy/cut"));
        m_controlCToCopyCheckBox = WidgetsFactory::createCheckBox(label, this);
        m_controlCToCopyCheckBox->setToolTip(tr("Use Ctrl+C/X to copy/cut text"));
        mainLayout->addRow(m_controlCToCopyCheckBox);
        addSearchItem(label, m_controlCToCopyCheckBox->toolTip(), m_controlCToCopyCheckBox);
        connect(m_controlCToCopyCheckBox, &QCheckBox::stateChanged,
                this, &ViPage::pageIsChanged);
    }
}

void ViPage::loadInternal()
{
    const auto &viConfig = ConfigMgr::getInst().getEditorConfig().getViConfig();

    m_controlCToCopyCheckBox->setChecked(viConfig->m_controlCToCopy);
}

bool ViPage::saveInternal()
{
    auto &editorConfig = ConfigMgr::getInst().getEditorConfig();
    auto &viConfig = editorConfig.getViConfig();

    viConfig->m_controlCToCopy = m_controlCToCopyCheckBox->isChecked();

    editorConfig.update();

    EditorPage::notifyEditorConfigChange();

    return true;
}

QString ViPage::title() const
{
    return tr("Vi Input Mode");
}
