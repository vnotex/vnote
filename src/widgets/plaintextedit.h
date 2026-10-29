#ifndef PLAINTEXTEDIT_H
#define PLAINTEXTEDIT_H

#include <QPlainTextEdit>

namespace vnotex {
// A plain text edit with selection-first Ctrl+W/U backward deletion.
class PlainTextEdit : public QPlainTextEdit {
  Q_OBJECT
public:
  explicit PlainTextEdit(QWidget *p_parent = nullptr);

protected:
  bool event(QEvent *p_event) Q_DECL_OVERRIDE;

  void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;
};
} // namespace vnotex

#endif // PLAINTEXTEDIT_H
