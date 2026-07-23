#ifndef STATUSBAR_H
#define STATUSBAR_H

#include <functional>

#include <QHash>
#include <QIcon>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QHBoxLayout;
class QStackedLayout;
class QLabel;
class QTimer;

namespace vnotex {

// The kind of a single status bar column. Fixed at registration; never mutated
// by the runtime setters.
enum class StatusBarColumnType { Label, Button, Menu, Edit, Spacer, Widget };

// Declarative description of one entry in a structured Menu column. Supports
// checkable items, separators, an exclusive (radio) group, and an opaque data
// payload.
struct StatusBarMenuItem {
  QString text;
  bool separator = false;
  bool checkable = false;
  bool checked = false;
  int exclusiveGroupId = -1; // >=0 => member of a QActionGroup keyed by id.
  QString data;              // Opaque payload (e.g. dictionary language code).
};

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
  QVector<StatusBarMenuItem> menuItems; // Menu items.
  int stretch = 1;          // Spacer stretch factor.

  // Callbacks (owning ViewWindow sets these when building the def):
  std::function<void()> onClicked;                   // Button.
  std::function<void(int itemIndex, bool checked)> onMenuTriggered; // Menu.
  std::function<void(const QString &)> onTextEdited; // Edit.
};

using StatusBarDef = QVector<StatusBarColumn>;

// A reusable, column-based status bar. Built once from a StatusBarDef, it lays
// out one child widget per column in index order inside a single QHBoxLayout.
// Column types are fixed at registration; only per-column data can be mutated
// at runtime via the granular update-by-index setters (all bounds-checked and
// type-checked, so a wrong-index or wrong-type call is a safe no-op).
//
// The columns live on a host page inside a QStackedLayout; a second page hosts
// a transient message label (see showMessage).
class StatusBar : public QWidget {
  Q_OBJECT
public:
  explicit StatusBar(const StatusBarDef &p_def, QWidget *p_parent = nullptr);

  ~StatusBar() override;

  // Granular update-by-index setters. Each mutates the stored column and
  // refreshes only that column's widget. Out-of-range index or wrong-type
  // calls are safe no-ops.
  void setColumnText(int p_index, const QString &p_text);       // Label/Button/Edit.
  void setColumnVisible(int p_index, bool p_visible);
  void setColumnStyle(int p_index, const QString &p_qss);
  void setColumnIcon(int p_index, const QIcon &p_icon);         // Button/Menu.
  void setColumnMenuItems(int p_index, const QVector<StatusBarMenuItem> &p_items); // Menu.

  // Widget column setters. Passing null detaches the current widget and leaves
  // the column empty/hidden. On swap, the previously hosted widget is detached
  // (reparented to null), never deleted.
  // Shared overload: keeps the widget alive via a retained QSharedPointer (e.g.
  // the editor-owned Vi input-mode widget). Such widgets are detached in the
  // destructor so the real owner is not left with a dangling child.
  void setColumnWidget(int p_index, const QSharedPointer<QWidget> &p_widget);
  // Raw overload: reparents the widget under the bar's column mount, so the bar
  // becomes its Qt parent and will destroy it with the bar (unless it is first
  // detached by a swap/null call). Pass only a widget with no other owner/deleter
  // (e.g. an encoding button held only as a raw observer pointer).
  void setColumnWidget(int p_index, QWidget *p_widget);

  // Show a transient message that overlays the columns, restoring the columns
  // after p_milliseconds (or immediately on an empty message).
  void showMessage(const QString &p_msg, int p_milliseconds = 3000);

private:
  // Build one widget for a column and wire its callbacks. Spacers add a
  // stretch and store a nullptr widget pointer.
  void buildColumn(const StatusBarColumn &p_column);

  // Rebuild a Menu column's QMenu from its menuItems.
  void rebuildMenu(int p_index);

  // Detach whatever is currently hosted in a Widget column mount.
  void detachColumnWidget(int p_index);

  // The mount layout for a Widget column (nullptr if not a Widget column).
  QHBoxLayout *widgetColumnMountLayout(int p_index) const;

  void clearMessage();

  bool isValidIndex(int p_index) const;

  // Top-level stack: page 0 = columns host, page 1 = message label.
  QStackedLayout *m_stackLayout = nullptr;

  // Host page carrying the columns HBox layout.
  QWidget *m_columnsHost = nullptr;

  QHBoxLayout *m_layout = nullptr;

  QLabel *m_messageLabel = nullptr;

  QTimer *m_messageTimer = nullptr;

  // Stored column defs, in registration order.
  QVector<StatusBarColumn> m_columns;

  // Parallel widget pointers keyed by the same index. nullptr for Spacer.
  // For a Widget column this is the mount container.
  QVector<QWidget *> m_widgets;

  // Shared ownership held per Widget column (empty for raw-mounted widgets).
  QHash<int, QSharedPointer<QWidget>> m_widgetColumnShared;
};

} // namespace vnotex

#endif // STATUSBAR_H
