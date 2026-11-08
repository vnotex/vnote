#ifndef SIMPLEMARKDOWNEDITOR_H
#define SIMPLEMARKDOWNEDITOR_H

#include <vtextedit/vmarkdowneditor.h>

namespace vnotex {
class ServiceLocator;

// In-memory Markdown source editor. Requires ThemeService, but no buffer or view window.
// Like setPlainText(), inherited setText() loads literal source, never auto-detected HTML.
class SimpleMarkdownEditor : public vte::VMarkdownEditor {
  Q_OBJECT
public:
  explicit SimpleMarkdownEditor(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  QString toPlainText() const;

public slots:
  void setPlainText(const QString &p_text);

signals:
  void textChanged();

private:
  ServiceLocator &m_services;
};
} // namespace vnotex

#endif // SIMPLEMARKDOWNEDITOR_H
