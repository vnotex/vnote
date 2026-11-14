#ifndef SIMPLEMARKDOWNEDITOR_H
#define SIMPLEMARKDOWNEDITOR_H

#include <vtextedit/vmarkdowneditor.h>

class QShortcut;
class QToolBar;

namespace vnotex {
class ServiceLocator;

// In-memory Markdown source editor. Requires ThemeService and ConfigMgr2, but no buffer or view
// window. Like setPlainText(), inherited setText() loads literal source, never auto-detected HTML.
class SimpleMarkdownEditor : public vte::VMarkdownEditor {
  Q_OBJECT
public:
  explicit SimpleMarkdownEditor(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  QString toPlainText() const;

public slots:
  void setPlainText(const QString &p_text);

signals:
  void textChanged();

protected:
  bool eventFilter(QObject *p_object, QEvent *p_event) override;

private:
  void setupToolBar();
  void refreshToolBarIcons();
  void typeLink();
  void typeTable();

  ServiceLocator &m_services;
  QToolBar *m_toolBar = nullptr;
  QList<QShortcut *> m_formattingShortcuts;
};
} // namespace vnotex

#endif // SIMPLEMARKDOWNEDITOR_H
