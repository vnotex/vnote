#include "miscpage.h"

#include <QFormLayout>

#include <core/servicelocator.h>
#include <widgets/widgetsfactory.h>

using namespace vnotex;

MiscPage::MiscPage(ServiceLocator &p_services, QWidget *p_parent)
    : SettingsPage(p_services, p_parent) {
  setupUI();
}

void MiscPage::setupUI() {}

void MiscPage::loadInternal() {}

bool MiscPage::saveInternal() { return true; }

QString MiscPage::title() const { return tr("Misc"); }
