#ifndef EXPORTRENDERERFACTORY_H
#define EXPORTRENDERERFACTORY_H

#include <QHash>
#include <QString>
#include <functional>

class QObject;
class QWidget;

namespace vnotex {
class ExportRenderer;
class ServiceLocator;

class ExportRendererFactory {
public:
  using Creator = std::function<ExportRenderer *(ServiceLocator &, QWidget *, QObject *)>;

  explicit ExportRendererFactory(ServiceLocator &p_services);
  void registerRenderer(const QString &p_fileTypeName, Creator p_creator);
  ExportRenderer *create(const QString &p_fileTypeName, QWidget *p_widgetParent,
                         QObject *p_parent) const;

private:
  ServiceLocator &m_services;
  QHash<QString, Creator> m_creators;
};

void registerBuiltinExportRenderers(ExportRendererFactory &p_factory);
} // namespace vnotex

#endif // EXPORTRENDERERFACTORY_H
