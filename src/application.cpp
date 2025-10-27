#include "application.h"

#include <QDebug>
#include <QDir>
#include <QFileOpenEvent>
#include <QFileSystemWatcher>
#include <QStyle>
#include <QTimer>
#include <core/vnotex.h>

using namespace vnotex;

Application::Application(int &p_argc, char **p_argv) : QApplication(p_argc, p_argv) {}

void Application::watchThemeFolder(const QString &p_themeFolderPath) {
  if (p_themeFolderPath.isEmpty()) {
    return;
  }

  // Initialize watchers only when needed
  if (!m_styleWatcher) {
    m_styleWatcher = new QFileSystemWatcher(this);
  }
  if (!m_reloadTimer) {
    m_reloadTimer = new QTimer(this);
    m_reloadTimer->setSingleShot(true);
    m_reloadTimer->setInterval(500); // 500ms debounce delay
    connect(m_reloadTimer, &QTimer::timeout, this, &Application::reloadThemeResources);

    // Connect file watcher to timer
    connect(m_styleWatcher, &QFileSystemWatcher::directoryChanged, m_reloadTimer,
            qOverload<>(&QTimer::start));
    connect(m_styleWatcher, &QFileSystemWatcher::fileChanged, m_reloadTimer,
            qOverload<>(&QTimer::start));
  }

  // Watch the theme folder and its files
  m_styleWatcher->addPath(p_themeFolderPath);

  // Also watch individual files in the theme folder
  QDir themeDir(p_themeFolderPath);
  QStringList files = themeDir.entryList(QDir::Files);
  for (const QString &file : files) {
    m_styleWatcher->addPath(themeDir.filePath(file));
  }
}

void Application::reloadThemeResources() {
  VNoteX::getInst().getThemeMgr().refreshCurrentTheme();

  auto stylesheet = VNoteX::getInst().getThemeMgr().fetchQtStyleSheet();
  if (!stylesheet.isEmpty()) {
    setStyleSheet(stylesheet);
    style()->unpolish(this);
    style()->polish(this);
  }
}

bool Application::event(QEvent *p_event) {
  // On macOS, we need this to open file from Finder.
  if (p_event->type() == QEvent::FileOpen) {
    QFileOpenEvent *openEvent = static_cast<QFileOpenEvent *>(p_event);
    qDebug() << "request to open file" << openEvent->file();
    emit openFileRequested(openEvent->file());
  }

  return QApplication::event(p_event);
}
