#include "viewwindowfactory.h"

#include <QDebug>
#include <QtGlobal>

#include <widgets/markdownviewwindow2.h>
#include <widgets/mindmapviewwindow2.h>
#include <widgets/textviewwindow2.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
#include <widgets/pdfviewwindow2.h>
#endif

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
  // pdf.js v6 is ESM-only and its `legacy` dist targets Chrome 125+; it is served
  // over a custom `vxpdf://` QWebEngineUrlScheme that only exists on Qt 6. The
  // floor is therefore Qt 6.9 (Chromium 130). Leaving the type unregistered below
  // it makes ViewAreaController fall back to the system default PDF application.
  // See src/data/extra/web/pdf.js/AGENTS.md.
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
  registerCreator("Pdf",
                  [](ServiceLocator &p_services, const Buffer2 &p_buffer, QWidget *p_parent,
                     ViewWindowMode) -> ViewWindow2 * {
                    return new PdfViewWindow2(p_services, p_buffer, p_parent);
                  });
#endif
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
