#include "exportrenderer.h"

#include <QWidget>

using namespace vnotex;

ExportRenderer::ExportRenderer(ServiceLocator &p_services, QWidget *p_widgetParent,
                               QObject *p_parent)
    : QObject(p_parent), m_services(p_services), m_widgetParent(p_widgetParent) {}

ExportRenderer::~ExportRenderer() = default;
