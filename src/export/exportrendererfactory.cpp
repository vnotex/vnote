#include "exportrendererfactory.h"

#include <utility>

using namespace vnotex;

ExportRendererFactory::ExportRendererFactory(ServiceLocator &p_services) : m_services(p_services) {}

void ExportRendererFactory::registerRenderer(const QString &p_fileTypeName,
                                             ExportCapabilities p_capabilities, Creator p_creator) {
  m_registrations.insert(p_fileTypeName.toLower(),
                         {std::move(p_capabilities), std::move(p_creator)});
}

const ExportRendererFactory::Registration *
ExportRendererFactory::registration(const QString &p_fileTypeName) const {
  const auto key = p_fileTypeName.isEmpty() ? QStringLiteral("others") : p_fileTypeName.toLower();
  auto it = m_registrations.constFind(key);
  if (it == m_registrations.cend()) {
    it = m_registrations.constFind(QStringLiteral("others"));
  }
  return it == m_registrations.cend() ? nullptr : &it.value();
}

ExportCapabilities ExportRendererFactory::capabilities(const QString &p_fileTypeName) const {
  const auto *entry = registration(p_fileTypeName);
  return entry ? entry->capabilities : ExportCapabilities{};
}

ExportRenderer *ExportRendererFactory::create(const QString &p_fileTypeName,
                                              QWidget *p_widgetParent, QObject *p_parent) const {
  const auto *entry = registration(p_fileTypeName);
  return !entry || !entry->creator ? nullptr : entry->creator(m_services, p_widgetParent, p_parent);
}
