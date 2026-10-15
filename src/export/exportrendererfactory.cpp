#include "exportrendererfactory.h"

#include <utility>

using namespace vnotex;

ExportRendererFactory::ExportRendererFactory(ServiceLocator &p_services) : m_services(p_services) {}

void ExportRendererFactory::registerRenderer(const QString &p_fileTypeName, Creator p_creator) {
  m_creators.insert(p_fileTypeName.toLower(), std::move(p_creator));
}

ExportRenderer *ExportRendererFactory::create(const QString &p_fileTypeName,
                                              QWidget *p_widgetParent, QObject *p_parent) const {
  const auto key = p_fileTypeName.isEmpty() ? QStringLiteral("others") : p_fileTypeName.toLower();
  auto it = m_creators.constFind(key);
  if (it == m_creators.cend()) {
    it = m_creators.constFind(QStringLiteral("others"));
  }
  return it == m_creators.cend() || !it.value() ? nullptr
                                                : it.value()(m_services, p_widgetParent, p_parent);
}
