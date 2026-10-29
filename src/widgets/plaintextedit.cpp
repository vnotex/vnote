#include "plaintextedit.h"

#include <QKeyEvent>
#include <QTextCursor>

#include <gui/utils/widgetutils.h>

using namespace vnotex;

namespace {
bool isDeletionShortcut(const QKeyEvent *p_event) {
  return (p_event->key() == Qt::Key_W || p_event->key() == Qt::Key_U) &&
         WidgetUtils::isViControlModifier(p_event->modifiers());
}
} // namespace

PlainTextEdit::PlainTextEdit(QWidget *p_parent) : QPlainTextEdit(p_parent) {}

bool PlainTextEdit::event(QEvent *p_event) {
  if (p_event->type() == QEvent::ShortcutOverride && !isReadOnly() &&
      isDeletionShortcut(static_cast<QKeyEvent *>(p_event))) {
    p_event->accept();
    return true;
  }
  return QPlainTextEdit::event(p_event);
}

void PlainTextEdit::keyPressEvent(QKeyEvent *p_event) {
  if (isReadOnly() || !isDeletionShortcut(p_event)) {
    QPlainTextEdit::keyPressEvent(p_event);
    return;
  }

  auto cursor = textCursor();
  if (!cursor.hasSelection()) {
    cursor.movePosition(p_event->key() == Qt::Key_W ? QTextCursor::PreviousWord
                                                    : QTextCursor::StartOfBlock,
                        QTextCursor::KeepAnchor);
  }
  if (cursor.hasSelection()) {
    cursor.beginEditBlock();
    cursor.removeSelectedText();
    cursor.endEditBlock();
    setTextCursor(cursor);
    ensureCursorVisible();
  }
  p_event->accept();
}
