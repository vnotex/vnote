#ifndef STATUSBAR_H
#define STATUSBAR_H

#include <functional>

#include <QIcon>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QHBoxLayout;

namespace vnotex {

// The kind of a single status bar column. Fixed at registration; never mutated
// by the runtime setters.
enum class StatusBarColumnType { Label, Button, Menu, Edit, Spacer };

// Declarative description of one status bar column. A ViewWindow builds a
// StatusBarDef (an ordered list of these) and hands it to the StatusBar once.
struct StatusBarColumn {
  StatusBarColumnType type = StatusBarColumnType::Label;
  bool visible = true;
  QString style; // QSS applied to the column widget.

  // Shared/per-type data (unused fields simply ignored per type):
  QString text;             // Label / Button / Edit initial text.
  QIcon icon;               // Button / Menu.
  QString placeholder;      // Edit.
  QStringList menuActions;  // Menu action labels.
  int stretch = 1;          // Spacer stretch factor.

  // Callbacks (owning ViewWindow sets these when building the def):
  std::function<void()> onClicked;                   // Button.
  std::function<void(int actionIndex)> onTriggered;  // Menu (index into menuActions).
  std::function<void(const QString &)> onTextEdited; // Edit.
};

using StatusBarDef = QVector<StatusBarColumn>;

// A reusable, column-based status bar. Built once from a StatusBarDef, it lays
// out one child widget per column in index order inside a single QHBoxLayout.
// Column types are fixed at registration; only per-column data can be mutated
// at runtime via the granular update-by-index setters (all bounds-checked and
// type-checked, so a wrong-index or wrong-type call is a safe no-op).
class StatusBar : public QWidget {
  Q_OBJECT
public:
  explicit StatusBar(const StatusBarDef &p_def, QWidget *p_parent = nullptr);

  // Granular update-by-index setters. Each mutates the stored column and
  // refreshes only that column's widget. Out-of-range index or wrong-type
  // calls are safe no-ops.
  void setColumnText(int p_index, const QString &p_text);       // Label/Button/Edit.
  void setColumnVisible(int p_index, bool p_visible);
  void setColumnStyle(int p_index, const QString &p_qss);
  void setColumnIcon(int p_index, const QIcon &p_icon);         // Button/Menu.
  void setColumnMenuActions(int p_index, const QStringList &p_actions); // Menu.
  void setColumnEditText(int p_index, const QString &p_text);   // Edit.

private:
  // Build one widget for a column and wire its callbacks. Spacers add a
  // stretch and store a nullptr widget pointer.
  void buildColumn(const StatusBarColumn &p_column);

  // Rebuild a Menu column's QMenu from its menuActions, wiring onTriggered.
  void rebuildMenu(int p_index);

  bool isValidIndex(int p_index) const;

  QHBoxLayout *m_layout = nullptr;

  // Stored column defs, in registration order.
  QVector<StatusBarColumn> m_columns;

  // Parallel widget pointers keyed by the same index. nullptr for Spacer.
  QVector<QWidget *> m_widgets;
};

} // namespace vnotex

#endif // STATUSBAR_H
