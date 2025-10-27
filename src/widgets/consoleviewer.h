#ifndef CONSOLEVIEWER_H
#define CONSOLEVIEWER_H

#include <QFrame>

class QPlainTextEdit;

namespace vnotex {
class TitleBar;

class ConsoleViewer : public QFrame {
  Q_OBJECT
public:
  explicit ConsoleViewer(QWidget *p_parent = nullptr);

  void append(const QString &p_text);

  void clear();

private:
  void setupUI();

  void setupTitleBar(const QString &p_title, QWidget *p_parent = nullptr);

  TitleBar *m_titleBar = nullptr;

  QPlainTextEdit *m_consoleEdit = nullptr;
};
} // namespace vnotex

#endif // CONSOLEVIEWER_H
