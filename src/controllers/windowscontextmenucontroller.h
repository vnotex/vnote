#ifndef WINDOWSCONTEXTMENUCONTROLLER_H
#define WINDOWSCONTEXTMENUCONTROLLER_H

#include <QObject>

#ifdef Q_OS_WIN
#include <QProcess>
#include <QString>

namespace vnotex {
class ServiceLocator;

// One application-owned controller per locator, shared by transient Settings pages.
class WindowsContextMenuController : public QObject {
  Q_OBJECT

public:
  enum class Action { Add, Remove };

  explicit WindowsContextMenuController(ServiceLocator &p_services);
  ~WindowsContextMenuController() override;

  bool isBusy() const { return m_busy; }
  bool hasError() const { return m_error; }
  const QString &statusText() const { return m_statusText; }

  // Returns false if busy, shutting down, or a required installed helper is missing.
  bool start(Action p_action);

signals:
  // Includes completion and its result, retained for a subsequently opened page.
  void stateChanged();

private:
  void complete(bool p_success, const QString &p_message);
  void shutdown();

  ServiceLocator &m_services;
  QProcess m_process;
  Action m_action = Action::Add;
  bool m_busy = false;
  bool m_error = false;
  bool m_shuttingDown = false;
  QString m_statusText;
};
} // namespace vnotex
#endif // Q_OS_WIN

#endif // WINDOWSCONTEXTMENUCONTROLLER_H
