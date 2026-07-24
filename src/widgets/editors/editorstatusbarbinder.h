#ifndef EDITORSTATUSBARBINDER_H
#define EDITORSTATUSBARBINDER_H

#include <QObject>
#include <QSharedPointer>
#include <QVector>

#include "statusbar.h"

namespace vte {
class VTextEditor;
}

namespace vnotex {

// Reusable View-layer glue that builds the editor status columns and wires a
// vte::VTextEditor's signals to a column-based StatusBar. Holds no services.
//
// Fixed column order (mirrors the embedded vtextedit StatusIndicator):
//   0 Widget  Vi input-mode widget
//   1 Label   cursor (line/col)
//   2 Spacer  stretch
//   3 Menu    spellcheck
//   4 Label   syntax
//   5 Label   mode
class EditorStatusBarBinder : public QObject {
  Q_OBJECT
public:
  explicit EditorStatusBarBinder(QObject *p_parent = nullptr);

  enum Column {
    ColViWidget = 0,
    ColCursor = 1,
    ColSpacer = 2,
    ColSpellCheck = 3,
    ColSyntax = 4,
    ColMode = 5,
  };

  // Build the 6-column def for @p_editor. Also records the editor so the
  // spellcheck callback can drive its setters. Call before setStatusBarDef().
  StatusBarDef buildDef(vte::VTextEditor *p_editor);

  // Connect the editor's signals to the bar and perform an initial sync.
  void attach(vte::VTextEditor *p_editor, StatusBar *p_bar);

private:
  void syncCursor();
  void syncSyntax();
  void syncMode();
  void syncSpellCheck();
  void syncInputModeWidget(const QSharedPointer<QWidget> &p_widget);

  static QString formatCursorText(vte::VTextEditor *p_editor);

  // Editor mode label text with the " (Vi)" suffix stripped.
  static QString formatModeText(vte::VTextEditor *p_editor);

  QVector<StatusBarMenuItem> buildSpellCheckItems() const;

  vte::VTextEditor *m_editor = nullptr;

  StatusBar *m_bar = nullptr;
};

} // namespace vnotex

#endif // EDITORSTATUSBARBINDER_H
