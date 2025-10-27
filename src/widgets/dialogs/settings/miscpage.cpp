#include "miscpage.h"

#include <QFormLayout>

#include <core/configmgr.h>
#include <core/coreconfig.h>
#include <core/sessionconfig.h>
#include <utils/widgetutils.h>
#include <widgets/widgetsfactory.h>

using namespace vnotex;

MiscPage::MiscPage(QWidget *p_parent) : SettingsPage(p_parent) { setupUI(); }

void MiscPage::setupUI() {}

void MiscPage::loadInternal() {}

bool MiscPage::saveInternal() { return true; }

QString MiscPage::title() const { return tr("Misc"); }
