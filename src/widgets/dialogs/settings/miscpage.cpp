#include "miscpage.h"

#include <QFormLayout>

#include <widgets/widgetsfactory.h>
#include <core/coreconfig.h>
#include <core/sessionconfig.h>
#include <core/configmgr.h>
#include <utils/widgetutils.h>

using namespace vnotex;

MiscPage::MiscPage(QWidget *p_parent)
    : SettingsPage(p_parent)
{
    setupUI();
}

void MiscPage::setupUI()
{
}

void MiscPage::loadInternal()
{

}

void MiscPage::saveInternal()
{

}

QString MiscPage::title() const
{
    return tr("Misc");
}
