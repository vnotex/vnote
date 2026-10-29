#ifndef EXPORTRENDERERFACTORY_H
#define EXPORTRENDERERFACTORY_H

#include "exportrenderer.h"
#include <QHash>
#include <QString>
#include <functional>

class QObject;
class QWidget;

namespace vnotex {
class ServiceLocator;

class ExportRendererFactory {
public:
  using Creator = std::function<ExportRenderer *(ServiceLocator &, QWidget *, QObject *)>;

  explicit ExportRendererFactory(ServiceLocator &p_services);
  void registerRenderer(const QString &p_fileTypeName, ExportCapabilities p_capabilities,
                        Creator p_creator);
  ExportCapabilities capabilities(const QString &p_fileTypeName) const;
  ExportRenderer *create(const QString &p_fileTypeName, QWidget *p_widgetParent,
                         QObject *p_parent) const;

private:
  struct Registration {
    ExportCapabilities capabilities;
    Creator creator;
  };

  const Registration *registration(const QString &p_fileTypeName) const;

  ServiceLocator &m_services;
  QHash<QString, Registration> m_registrations;
};

void registerBuiltinExportRenderers(ExportRendererFactory &p_factory);
} // namespace vnotex

#endif // EXPORTRENDERERFACTORY_H
