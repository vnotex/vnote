#include "viewwindowtoolbarhelper.h"

#include <QToolBar>
#include <QAction>
#include <QShortcut>
#include <QKeySequence>
#include <QCoreApplication>
#include <QToolButton>
#include <QMenu>
#include <QDebug>
#include <QActionGroup>

#include "toolbarhelper.h"
#include <utils/iconutils.h>
#include <utils/widgetutils.h>
#include <core/configmgr.h>
#include <core/editorconfig.h>
#include "editreaddiscardaction.h"
#include "widgetsfactory.h"
#include "attachmentpopup.h"
#include "propertydefs.h"
#include "outlinepopup.h"
#include "viewwindow.h"
#include <core/global.h>

using namespace vnotex;

typedef EditorConfig::Shortcut Shortcut;

// To get the right shortcut context, we use a separate QShrotcut for the action shortcut.
// @p_parentAction: the parent action of @p_action which is in a menu of @p_parentAction.
void ViewWindowToolBarHelper::addActionShortcut(QAction *p_action,
                                                const QString &p_shortcut,
                                                QWidget *p_widget,
                                                QAction *p_parentAction)
{
    auto shortcut = WidgetUtils::createShortcut(p_shortcut, p_widget, Qt::WidgetWithChildrenShortcut);
    if (!shortcut) {
        return;
    }

    QObject::connect(shortcut, &QShortcut::activated,
                     p_action, [p_action, p_parentAction]() {
                         if (p_action->isEnabled()) {
                             if (p_parentAction) {
                                 if (p_parentAction->isEnabled()) {
                                     p_action->trigger();
                                 }
                             } else {
                                 p_action->trigger();
                             }
                         }
                     });
    p_action->setText(QString("%1\t%2").arg(p_action->text(), shortcut->key().toString(QKeySequence::NativeText)));
}

void ViewWindowToolBarHelper::addButtonShortcut(QToolButton *p_btn,
                                                const QString &p_shortcut,
                                                QWidget *p_widget)
{
    auto shortcut = WidgetUtils::createShortcut(p_shortcut, p_widget, Qt::WidgetWithChildrenShortcut);
    if (!shortcut) {
        return;
    }

    QObject::connect(shortcut, &QShortcut::activated,
                     p_btn, [p_btn]() {
                         if (p_btn->isEnabled()) {
                             p_btn->click();
                         }
                     });
    auto act = p_btn->defaultAction();
    if (act) {
        act->setText(QString("%1\t%2").arg(act->text(), shortcut->key().toString(QKeySequence::NativeText)));
    } else {
        p_btn->setText(QString("%1\t%2").arg(p_btn->text(), shortcut->key().toString(QKeySequence::NativeText)));
    }
}

QAction *ViewWindowToolBarHelper::addAction(QToolBar *p_tb, Action p_action)
{
    auto viewWindow = static_cast<QWidget *>(p_tb->parent());
    const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();

    QAction *act = nullptr;
    switch (p_action) {
    case Action::Save:
        act = p_tb->addAction(ToolBarHelper::generateIcon("save_editor.svg"),
                              ViewWindow::tr("Save"));
        addActionShortcut(act, editorConfig.getShortcut(Shortcut::Save), viewWindow);
        break;

    case Action::EditReadDiscard:
    {
        auto erdAct = new EditReadDiscardAction(ToolBarHelper::generateIcon("edit_editor.svg"),
                                                ViewWindow::tr("Edit"),
                                                ToolBarHelper::generateIcon("read_editor.svg"),
                                                ViewWindow::tr("Read"),
                                                ToolBarHelper::generateIcon("discard_editor.svg"),
                                                ViewWindow::tr("Discard"),
                                                p_tb);
        act = erdAct;
        addActionShortcut(erdAct, editorConfig.getShortcut(Shortcut::EditRead), viewWindow);

        auto discardAct = erdAct->getDiscardAction();
        addActionShortcut(discardAct, editorConfig.getShortcut(Shortcut::Discard), viewWindow);

        p_tb->addAction(erdAct);

        auto toolBtn = dynamic_cast<QToolButton *>(p_tb->widgetForAction(erdAct));
        Q_ASSERT(toolBtn);
        erdAct->setToolButtonForAction(toolBtn);
        break;
    }

    case Action::TypeHeading:
    {
        act = p_tb->addAction(ToolBarHelper::generateIcon("type_heading_editor.svg"),
                              ViewWindow::tr("Heading"));

        auto toolBtn = dynamic_cast<QToolButton *>(p_tb->widgetForAction(act));
        Q_ASSERT(toolBtn);
        toolBtn->setPopupMode(QToolButton::InstantPopup);
        toolBtn->setProperty(PropertyDefs::s_toolButtonWithoutMenuIndicator, true);

        auto menu = WidgetsFactory::createMenu(p_tb);

        auto act1 = menu->addAction(ViewWindow::tr("Heading 1"));
        addActionShortcut(act1,
                          editorConfig.getShortcut(EditorConfig::Shortcut::TypeHeading1),
                          viewWindow,
                          act);
        act1->setData(1);

        auto act2 = menu->addAction(ViewWindow::tr("Heading 2"));
        addActionShortcut(act2,
                          editorConfig.getShortcut(EditorConfig::Shortcut::TypeHeading2),
                          viewWindow,
                          act);
        act2->setData(2);

        auto act3 = menu->addAction(ViewWindow::tr("Heading 3"));
        addActionShortcut(act3,
                          editorConfig.getShortcut(EditorConfig::Shortcut::TypeHeading3),
                          viewWindow,
                          act);
        act3->setData(3);

        auto act4 = menu->addAction(ViewWindow::tr("Heading 4"));
        addActionShortcut(act4,
                          editorConfig.getShortcut(EditorConfig::Shortcut::TypeHeading4),
                          viewWindow,
                          act);
        act4->setData(4);

        auto act5 = menu->addAction(ViewWindow::tr("Heading 5"));
        addActionShortcut(act5,
                          editorConfig.getShortcut(EditorConfig::Shortcut::TypeHeading5),
                          viewWindow,
                          act);
        act5->setData(5);

        auto act6 = menu->addAction(ViewWindow::tr("Heading 6"));
        addActionShortcut(act6,
                          editorConfig.getShortcut(EditorConfig::Shortcut::TypeHeading6),
                          viewWindow,
                          act);
        act6->setData(6);

        auto act7 = menu->addAction(ViewWindow::tr("Clear"));
        addActionShortcut(act7,
                          editorConfig.getShortcut(EditorConfig::Shortcut::TypeHeadingNone),
                          viewWindow,
                          act);
        act7->setData(7);

        toolBtn->setMenu(menu);
        break;
    }

    case Action::TypeBold:
        act = p_tb->addAction(ToolBarHelper::generateIcon("type_bold_editor.svg"),
                              ViewWindow::tr("Bold"));
        addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeBold), viewWindow);
        break;

    case Action::TypeItalic:
        act = p_tb->addAction(ToolBarHelper::generateIcon("type_italic_editor.svg"),
                              ViewWindow::tr("Italic"));
        addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeItalic), viewWindow);
        break;

    case Action::TypeStrikethrough:
        act = p_tb->addAction(ToolBarHelper::generateIcon("type_strikethrough_editor.svg"),
                              ViewWindow::tr("Strikethrough"));
        addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeStrikethrough), viewWindow);
        break;

    case Action::TypeUnorderedList:
        act = p_tb->addAction(ToolBarHelper::generateIcon("type_unordered_list_editor.svg"),
                              ViewWindow::tr("Unordered List"));
        addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeUnorderedList), viewWindow);
        break;

    case Action::TypeOrderedList:
        act = p_tb->addAction(ToolBarHelper::generateIcon("type_ordered_list_editor.svg"),
                              ViewWindow::tr("Ordered List"));
        addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeOrderedList), viewWindow);
        break;

    case Action::TypeTodoList:
        act = p_tb->addAction(ToolBarHelper::generateIcon("type_todo_list_editor.svg"),
                              ViewWindow::tr("Todo List"));
        addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeTodoList), viewWindow);
        break;

    case Action::TypeCheckedTodoList:
        act = p_tb->addAction(ToolBarHelper::generateIcon("type_checked_todo_list_editor.svg"),
                              ViewWindow::tr("Checked Todo List"));
        addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeCheckedTodoList), viewWindow);
        break;

    case Action::TypeCode:
        act = p_tb->addAction(ToolBarHelper::generateIcon("type_code_editor.svg"),
                              ViewWindow::tr("Code"));
        addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeCode), viewWindow);
        break;

    case Action::TypeCodeBlock:
        act = p_tb->addAction(ToolBarHelper::generateIcon("type_code_block_editor.svg"),
                              ViewWindow::tr("Code Block"));
        addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeCodeBlock), viewWindow);
        break;

    case Action::TypeMath:
        act = p_tb->addAction(ToolBarHelper::generateIcon("type_math_editor.svg"),
                              ViewWindow::tr("Math"));
        addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeMath), viewWindow);
        break;

    case Action::TypeMathBlock:
        act = p_tb->addAction(ToolBarHelper::generateIcon("type_math_block_editor.svg"),
                              ViewWindow::tr("Math Block"));
        addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeMathBlock), viewWindow);
        break;

    case Action::TypeQuote:
        act = p_tb->addAction(ToolBarHelper::generateIcon("type_quote_editor.svg"),
                              ViewWindow::tr("Quote"));
        addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeQuote), viewWindow);
        break;

    case Action::TypeLink:
        act = p_tb->addAction(ToolBarHelper::generateIcon("type_link_editor.svg"),
                              ViewWindow::tr("Link"));
        addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeLink), viewWindow);
        break;

    case Action::TypeImage:
        act = p_tb->addAction(ToolBarHelper::generateIcon("type_image_editor.svg"),
                              ViewWindow::tr("Image"));
        addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeImage), viewWindow);
        break;

    case Action::TypeTable:
        act = p_tb->addAction(ToolBarHelper::generateIcon("type_table_editor.svg"),
                              ViewWindow::tr("Table"));
        addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeTable), viewWindow);
        break;

    case Action::TypeMark:
        act = p_tb->addAction(ToolBarHelper::generateIcon("type_mark_editor.svg"),
                              ViewWindow::tr("Mark"));
        addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeMark), viewWindow);
        break;

    case Action::Attachment:
    {
        act = p_tb->addAction(ToolBarHelper::generateIcon("attachment_editor.svg"),
                              ViewWindow::tr("Attachments"));

        auto toolBtn = dynamic_cast<QToolButton *>(p_tb->widgetForAction(act));
        Q_ASSERT(toolBtn);
        toolBtn->setPopupMode(QToolButton::InstantPopup);
        toolBtn->setProperty(PropertyDefs::s_toolButtonWithoutMenuIndicator, true);

        auto menu = new AttachmentPopup(toolBtn, p_tb);
        toolBtn->setMenu(menu);
        break;
    }

    case Action::Outline:
    {
        act = p_tb->addAction(ToolBarHelper::generateIcon("outline_editor.svg"),
                              ViewWindow::tr("Outline"));

        auto toolBtn = dynamic_cast<QToolButton *>(p_tb->widgetForAction(act));
        Q_ASSERT(toolBtn);
        toolBtn->setPopupMode(QToolButton::InstantPopup);
        toolBtn->setProperty(PropertyDefs::s_toolButtonWithoutMenuIndicator, true);

        addButtonShortcut(toolBtn, editorConfig.getShortcut(Shortcut::Outline), viewWindow);

        auto menu = new OutlinePopup(toolBtn, p_tb);
        toolBtn->setMenu(menu);
        break;
    }

    case Action::FindAndReplace:
    {
        act = p_tb->addAction(ToolBarHelper::generateIcon("find_replace_editor.svg"),
                              ViewWindow::tr("Find And Replace"));
        addActionShortcut(act, editorConfig.getShortcut(Shortcut::FindAndReplace), viewWindow);
        break;
    }

    case Action::SectionNumber:
    {
        act = p_tb->addAction(ToolBarHelper::generateIcon("section_number_editor.svg"),
                              ViewWindow::tr("Section Number"));

        auto toolBtn = dynamic_cast<QToolButton *>(p_tb->widgetForAction(act));
        Q_ASSERT(toolBtn);
        toolBtn->setPopupMode(QToolButton::InstantPopup);
        toolBtn->setProperty(PropertyDefs::s_toolButtonWithoutMenuIndicator, true);

        auto menu = WidgetsFactory::createMenu(p_tb);

        auto actGroup = new QActionGroup(menu);
        auto act1 = actGroup->addAction(ViewWindow::tr("Follow Configuration"));
        act1->setCheckable(true);
        act1->setChecked(true);
        act1->setData(OverrideState::NoOverride);
        menu->addAction(act1);

        act1 = actGroup->addAction(ViewWindow::tr("Enabled"));
        act1->setCheckable(true);
        act1->setData(OverrideState::ForceEnable);
        menu->addAction(act1);

        act1 = actGroup->addAction(ViewWindow::tr("Disabled"));
        act1->setCheckable(true);
        act1->setData(OverrideState::ForceDisable);
        menu->addAction(act1);

        toolBtn->setMenu(menu);
        break;
    }

    default:
        Q_ASSERT(false);
        break;
    }

    return act;
}
