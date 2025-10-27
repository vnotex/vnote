#ifndef WIDGETUTILS_H
#define WIDGETUTILS_H

#include <QList>
#include <QMessageBox>
#include <QModelIndex>
#include <QSize>
#include <QVariant>
#include <QtGlobal>

class QWidget;
class QAbstractScrollArea;
class QKeyEvent;
class QActionGroup;
class QAction;
class QScrollArea;
class QListView;
class QMenu;
class QShortcut;
class QLineEdit;
class QLayout;
class QPushButton;
class QSplitter;
class QScreen;
class QFormLayout;

namespace vnotex {
class WidgetUtils {
public:
  WidgetUtils() = delete;

  static void setPropertyDynamically(QWidget *p_widget, const char *p_prop,
                                     const QVariant &p_val = QVariant());

  static void updateStyle(QWidget *p_widget);

  static qreal calculateScaleFactor(const QScreen *p_screen = nullptr);

  static bool isScrollBarVisible(QAbstractScrollArea *p_widget, bool p_horizontal);

  static QSize availableScreenSize(QWidget *p_widget);

  static void openUrlByDesktop(const QUrl &p_url);

  // Given @p_event, try to process it by injecting proper event instead if it
  // triggers Vi operation.
  // Return true if @p_event is handled properly.
  // @p_escTargetWidget: the widget to accept the ESC event.
  static bool processKeyEventLikeVi(QWidget *p_widget, QKeyEvent *p_event,
                                    QWidget *p_escTargetWidget = nullptr);

  static bool isViControlModifier(int p_modifiers);

  static bool isMetaKey(int p_key);

  static void clearActionGroup(QActionGroup *p_actGroup);

  static void addActionShortcut(QAction *p_action, const QString &p_shortcut,
                                Qt::ShortcutContext p_context = Qt::WindowShortcut);

  // Just add a shortcut text hint to the action.
  static void addActionShortcutText(QAction *p_action, const QString &p_shortcut);

  static void addButtonShortcutText(QPushButton *p_button, const QString &p_shortcut);

  static QShortcut *createShortcut(const QString &p_shortcut, QWidget *p_widget,
                                   Qt::ShortcutContext p_context = Qt::WindowShortcut);

  static void updateSize(QWidget *p_widget);

  static void resizeToHideScrollBarLater(QScrollArea *p_scroll, bool p_vertical, bool p_horizontal);

  static QVector<QModelIndex> getVisibleIndexes(const QListView *p_view);

  static QString getMonospaceFont();

  static QAction *findActionByObjectName(const QList<QAction *> &p_actions,
                                         const QString &p_objName);

  static void insertActionAfter(QMenu *p_menu, QAction *p_after, QAction *p_action);

  // Select the base name part of the line edit content.
  static void selectBaseName(QLineEdit *p_lineEdit);

  static void setContentsMargins(QLayout *p_layout);

  static bool distributeWidgetsOfSplitter(QSplitter *p_splitter);

  static void clearLayout(QFormLayout *p_layout);

  static bool isOrAncestorOf(const QWidget *p_widget, const QWidget *p_child);

private:
  static void resizeToHideScrollBar(QScrollArea *p_scroll, bool p_vertical, bool p_horizontal);
};
} // namespace vnotex

#endif // WIDGETUTILS_H
