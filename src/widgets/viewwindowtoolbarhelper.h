#ifndef VIEWWINDOWTOOLBARHELPER_H
#define VIEWWINDOWTOOLBARHELPER_H

class QToolBar;
class QAction;
class QWidget;
class QString;
class QToolButton;

#include <core/global.h>

namespace vnotex {
// Help to setup common buttons of ViewWindow tool bar.
// DEPRECATED: Use ViewWindowToolBarHelper2 with ServiceLocator pattern instead.
// This class depends on legacy singletons (ConfigMgr, ThemeMgr via ToolBarHelper).
class VNOTEX_DEPRECATED("Use ViewWindowToolBarHelper2 with ServiceLocator pattern instead")
    ViewWindowToolBarHelper {
public:
  enum Action {
    Save,
    EditReadDiscard,
    ViewMode,

    // Make sure they are put together.
    // Including Heading1-6 and HeadingNone.
    TypeHeading,
    TypeBold,
    TypeItalic,
    TypeStrikethrough,
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
    TypeMark,
    // Ending TypeXXX.
    TypeMax,

    Attachment,
    Tag,
    Outline,
    FindAndReplace,
    SectionNumber,
    InplacePreview,
    ImageHost,
    Debug,
    Print,
    WordCount
  };

  static QAction *addAction(QToolBar *p_tb, Action p_action);

  static void addActionShortcut(QAction *p_action, const QString &p_shortcut, QWidget *p_widget,
                                QAction *p_parentAction = nullptr);

  static void addButtonShortcut(QToolButton *p_btn, const QString &p_shortcut, QWidget *p_widget);

  ViewWindowToolBarHelper() = delete;
};
} // namespace vnotex

#endif // VIEWWINDOWTOOLBARHELPER_H
