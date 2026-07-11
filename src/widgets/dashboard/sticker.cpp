#include "sticker.h"

using namespace vnotex;

Sticker::Sticker(ServiceLocator &p_services, QWidget *p_parent)
    : QWidget(p_parent), m_services(p_services) {}
