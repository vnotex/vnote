#include "workspacewrapper.h"

#include <QDebug>

using namespace vnotex;

WorkspaceWrapper::WorkspaceWrapper(const QString &p_workspaceId, QObject *p_parent)
    : QObject(p_parent), m_workspaceId(p_workspaceId) {
}

WorkspaceWrapper::~WorkspaceWrapper() {
  Q_ASSERT(m_viewWindows.isEmpty());
}

const QString &WorkspaceWrapper::workspaceId() const {
  return m_workspaceId;
}

void WorkspaceWrapper::receiveViewWindows(const QVector<QObject *> &p_windows,
                                          int p_currentIndex) {
  Q_ASSERT(m_viewWindows.isEmpty());
  m_viewWindows = p_windows;
  m_currentIndex = p_currentIndex;
}

QVector<QObject *> WorkspaceWrapper::takeAllViewWindows() {
  QVector<QObject *> windows;
  windows.swap(m_viewWindows);
  return windows;
}

const QVector<QObject *> &WorkspaceWrapper::viewWindows() const {
  return m_viewWindows;
}

int WorkspaceWrapper::viewWindowCount() const {
  return m_viewWindows.size();
}

bool WorkspaceWrapper::isVisible() const {
  return m_visible;
}

void WorkspaceWrapper::setVisible(bool p_visible) {
  m_visible = p_visible;
}

int WorkspaceWrapper::currentIndex() const {
  return m_currentIndex;
}

void WorkspaceWrapper::setCurrentIndex(int p_index) {
  m_currentIndex = p_index;
}
