#include "themepage.h"

#include <QComboBox>
#include <QVBoxLayout>

#include <widgets/widgetsfactory.h>

using namespace vnotex;

ThemePage::ThemePage(QWidget *p_parent)
    : SettingsPage(p_parent)
{
    setupUI();
}

void ThemePage::setupUI()
{
    auto mainLayout = new QVBoxLayout(this);
}

void ThemePage::loadInternal()
{
}

void ThemePage::saveInternal()
{
}

QString ThemePage::title() const
{
    return tr("Theme");
}
