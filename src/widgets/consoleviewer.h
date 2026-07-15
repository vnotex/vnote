#ifndef CONSOLEVIEWER_H
#define CONSOLEVIEWER_H

#include <QFrame>

#include <core/noncopyable.h>

class QPlainTextEdit;

namespace vnotex {
class ServiceLocator;
class TitleBar;

// Read-only console log widget that streams task execution output. Lives in the
// bottom ConsoleDock. Appends framed output verbatim from
// TaskService::taskOutputRequested; caps buffered blocks to bound memory.
class ConsoleViewer : public QFrame, private Noncopyable {
  Q_OBJECT

public:
  explicit ConsoleViewer(ServiceLocator &p_services, QWidget *p_parent = nullptr);
  ~ConsoleViewer() override = default;

public slots:
  // Append raw output text and scroll to the end.
  void appendOutput(const QString &p_text);

  // Clear all buffered output.
  void clear();

private:
  void setupUI();
  void setupTitleBar();

  ServiceLocator &m_services;

  TitleBar *m_titleBar = nullptr;

  QPlainTextEdit *m_output = nullptr;
};

} // namespace vnotex
#endif // CONSOLEVIEWER_H
