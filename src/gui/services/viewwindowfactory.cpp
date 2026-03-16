#include "viewwindowfactory.h"

#include <QDebug>

#include <widgets/textviewwindow2.h>

using namespace vnotex;

ViewWindowFactory::ViewWindowFactory(QObject *p_parent)
    : QObject(p_parent) {
}

ViewWindowFactory::~ViewWindowFactory() {
}

void ViewWindowFactory::registerBuiltInCreators() {
  registerCreator("Text",
                  [](ServiceLocator &p_services, const Buffer2 &p_buffer,
                     QWidget *p_parent) -> ViewWindow2 * {
                    return new TextViewWindow2(p_services, p_buffer, p_parent);
                  });
  registerCreator("Others",
                  [](ServiceLocator &p_services, const Buffer2 &p_buffer,
                     QWidget *p_parent) -> ViewWindow2 * {
                    return new TextViewWindow2(p_services, p_buffer, p_parent);
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
                                       const Buffer2 &p_buffer, QWidget *p_parent) const {
  auto it = m_creators.find(p_fileType.toLower());
  if (it == m_creators.end()) {
    qWarning() << "ViewWindowFactory: no creator for file type" << p_fileType;
    return nullptr;
  }
  return it.value()(p_services, p_buffer, p_parent);
}
