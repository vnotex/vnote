#ifndef VIEWWINDOWTOOLBARHELPER_H
#define VIEWWINDOWTOOLBARHELPER_H

class QToolBar;
class QAction;
class QWidget;
class QString;
class QToolButton;

namespace vnotex
{
    // Help to setup common buttons of ViewWindow tool bar.
    class ViewWindowToolBarHelper
    {
    public:
        enum Action
        {
            Save,
            EditReadDiscard,

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
            Outline,
            FindAndReplace,
            SectionNumber
        };

        static QAction *addAction(QToolBar *p_tb, Action p_action);

        static void addActionShortcut(QAction *p_action,
                                      const QString &p_shortcut,
                                      QWidget *p_widget,
                                      QAction *p_parentAction = nullptr);

        static void addButtonShortcut(QToolButton *p_btn,
                                      const QString &p_shortcut,
                                      QWidget *p_widget);

        ViewWindowToolBarHelper() = delete;
    };
}

#endif // VIEWWINDOWTOOLBARHELPER_H
