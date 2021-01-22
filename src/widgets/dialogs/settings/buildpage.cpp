#include "buildpage.h"

using namespace vnotex;

BuildPage::BuildPage(QWidget *p_parent)
    : SettingsPage(p_parent)
{
    setupUI();
}

void BuildPage::setupUI()
{   
}

void BuildPage::loadInternal()
{
}

void BuildPage::saveInternal()
{
}

QString BuildPage::title() const
{
    return tr("Build");
}
