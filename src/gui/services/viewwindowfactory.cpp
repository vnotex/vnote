#include "viewwindowfactory.h"

#include <QDebug>

#include <widgets/markdownviewwindow2.h>
#include <widgets/mindmapviewwindow2.h>
#include <widgets/pdfviewwindow2.h>
#include <widgets/textviewwindow2.h>

using namespace vnotex;

ViewWindowFactory::ViewWindowFactory(QObject *p_parent) : QObject(p_parent) {}

ViewWindowFactory::~ViewWindowFactory() {}

void ViewWindowFactory::registerBuiltInCreators() {
  registerCreator("Markdown",
                  [](ServiceLocator &p_services, const Buffer2 &p_buffer, QWidget *p_parent,
                     ViewWindowMode p_mode) -> ViewWindow2 * {
                    return new MarkdownViewWindow2(p_services, p_buffer, p_parent, p_mode);
                  });
  registerCreator("Text",
                  [](ServiceLocator &p_services, const Buffer2 &p_buffer, QWidget *p_parent,
                     ViewWindowMode) -> ViewWindow2 * {
                    return new TextViewWindow2(p_services, p_buffer, p_parent);
                  });
  registerCreator("Others",
                  [](ServiceLocator &p_services, const Buffer2 &p_buffer, QWidget *p_parent,
                     ViewWindowMode) -> ViewWindow2 * {
                    return new TextViewWindow2(p_services, p_buffer, p_parent);
                  });
  registerCreator("Pdf",
                  [](ServiceLocator &p_services, const Buffer2 &p_buffer, QWidget *p_parent,
                     ViewWindowMode) -> ViewWindow2 * {
                    return new PdfViewWindow2(p_services, p_buffer, p_parent);
                  });
  registerCreator("MindMap",
                  [](ServiceLocator &p_services, const Buffer2 &p_buffer, QWidget *p_parent,
                     ViewWindowMode) -> ViewWindow2 * {
                    return new MindMapViewWindow2(p_services, p_buffer, p_parent);
                  });
  registerCreator(
      "Widget", [](ServiceLocator &, const Buffer2 &, QWidget *, ViewWindowMode) -> ViewWindow2 * {
        // Widget windows are created via ViewArea2::openWidgetContent(),
        // not the factory. This registration exists as documentation.
        Q_ASSERT(false);
        return nullptr;
      });
}

void ViewWindowFactory::registerCreator(const QString &p_fileType, CreatorFunc p_creator) {
  m_creators[p_fileType.toLower()] = std::move(p_creator);
}

void ViewWindowFactory::unregisterCreator(const QString &p_fileType) {
  m_creators.remove(p_fileType.toLower());
}

bool ViewWindowFactory::hasCreator(const QString &p_fileType) const {
  return m_creators.contains(p_fileType.toLower());
}

ViewWindow2 *ViewWindowFactory::create(const QString &p_fileType, ServiceLocator &p_services,
                                       const Buffer2 &p_buffer, QWidget *p_parent,
                                       ViewWindowMode p_mode) const {
  auto it = m_creators.find(p_fileType.toLower());
  if (it == m_creators.end()) {
    qWarning() << "ViewWindowFactory: no creator for file type" << p_fileType;
    return nullptr;
  }
  return it.value()(p_services, p_buffer, p_parent, p_mode);
}
