#ifndef VIEWWINDOWTOOLBARHELPER2_H
#define VIEWWINDOWTOOLBARHELPER2_H

#include <QIcon>

class QAction;
class QToolBar;
class QToolButton;
class QWidget;
class QString;

namespace vnotex {

class ServiceLocator;

// New-architecture toolbar helper for ViewWindow2.
// Uses DI (ServiceLocator) instead of legacy singletons.
// Replaces ViewWindowToolBarHelper for new architecture code.
class ViewWindowToolBarHelper2 {
public:
  // Same action enum as legacy ViewWindowToolBarHelper, but only
  // actions relevant to the new architecture are listed.
  // Add more as they are migrated.
  enum Action {
    Save,
    FindAndReplace,
    Print,
    WordCount,
    EditRead,

    // Formatting actions for Markdown.
    // TypeHeading includes H1-H6 + HeadingNone submenu.
    TypeHeading,
    TypeBold,
    TypeItalic,
    TypeStrikethrough,
    TypeMark,
    TypeUnorderedList,
    TypeOrderedList,
    TypeTodoList,
    TypeCheckedTodoList,
    TypeCode,
    TypeCodeBlock,
    TypeMath,
    TypeMathBlock,
    TypeQuote,
    TypeLink,
    TypeImage,
    TypeTable,

    // Layout mode toggle.
    ToggleLayoutMode,

    // Live preview toggle (Edit mode only).
    ToggleLivePreview,

    // Outline popup button.
    Outline
  };

  ViewWindowToolBarHelper2() = delete;

  // Create a toolbar action via DI.
  // @p_tb: The toolbar to add the action to.
  // @p_action: Which action to create.
  // @p_services: ServiceLocator for theme/config resolution.
  // @p_shortcutWidget: Widget for shortcut context (the ViewWindow2 instance).
  static QAction *addAction(QToolBar *p_tb, Action p_action,
                            ServiceLocator &p_services, QWidget *p_shortcutWidget);

  // Add a spacer widget to push subsequent actions to the right.
  static void addSpacer(QToolBar *p_tb);

  // Attach a keyboard shortcut to an action.
  // @p_parentAction: optional parent action (for actions in a submenu).
  static void addActionShortcut(QAction *p_action, const QString &p_shortcut,
                                QWidget *p_widget, QAction *p_parentAction = nullptr);

  // Attach a keyboard shortcut to a tool button.
  static void addButtonShortcut(QToolButton *p_btn, const QString &p_shortcut,
                                QWidget *p_widget);

  // Generate a QIcon from theme icon file name via DI.
  static QIcon generateIcon(ServiceLocator &p_services, const QString &p_iconName);
};

} // namespace vnotex

#endif // VIEWWINDOWTOOLBARHELPER2_H
