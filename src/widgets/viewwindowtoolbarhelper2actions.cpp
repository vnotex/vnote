#include "viewwindowtoolbarhelper2.h"

#include <QAction>
#include <QDebug>
#include <QMenu>
#include <QShortcut>
#include <QToolBar>
#include <QToolButton>

#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/servicelocator.h>
#include <gui/services/themeservice.h>
#include <gui/utils/iconutils.h>
#include <gui/utils/widgetutils.h>

using namespace vnotex;

using Shortcut = EditorConfig::Shortcut;

void ViewWindowToolBarHelper2::addActionShortcut(QAction *p_action, const QString &p_shortcut,
                                                 QWidget *p_widget, QAction *p_parentAction) {
  auto *shortcut =
      WidgetUtils::createShortcut(p_shortcut, p_widget, Qt::WidgetWithChildrenShortcut);
  if (!shortcut) {
    return;
  }

  QObject::connect(shortcut, &QShortcut::activated, p_action, [p_action, p_parentAction]() {
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
  QObject::connect(shortcut, &QShortcut::activatedAmbiguously, p_action, [p_action]() {
    qWarning() << "ViewWindow2 shortcut activated ambiguously" << p_action->text();
  });
  p_action->setText(QStringLiteral("%1\t%2").arg(
      p_action->text(), shortcut->key().toString(QKeySequence::NativeText)));
}

QIcon ViewWindowToolBarHelper2::generateIcon(ServiceLocator &p_services,
                                             const QString &p_iconName) {
  auto *themeService = p_services.get<ThemeService>();
  if (!themeService) {
    return QIcon();
  }
  const auto fg = themeService->paletteColor(QStringLiteral("widgets#toolbar#icon#fg"));
  const auto disabledFg =
      themeService->paletteColor(QStringLiteral("widgets#toolbar#icon#disabled#fg"));

  QVector<IconUtils::OverriddenColor> colors;
  colors.push_back(IconUtils::OverriddenColor(fg, QIcon::Normal));
  colors.push_back(IconUtils::OverriddenColor(disabledFg, QIcon::Disabled));

  auto iconFile = themeService->getIconFile(p_iconName);
  return IconUtils::fetchIcon(iconFile, colors);
}

QAction *ViewWindowToolBarHelper2::addFormattingAction(QToolBar *p_tb, Action p_action,
                                                       ServiceLocator &p_services,
                                                       QWidget *p_shortcutWidget) {
  auto *configMgr = p_services.get<ConfigMgr2>();
  Q_ASSERT(configMgr);
  const auto &editorConfig = configMgr->getEditorConfig();
  QAction *act = nullptr;
  switch (p_action) {
  case Action::TypeHeading: {
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("type_heading_editor.svg")),
                          QObject::tr("Heading 1"));
    act->setProperty("iconName", QStringLiteral("type_heading_editor.svg"));
    act->setData(1);
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeHeading1), p_shortcutWidget);

    auto *toolBtn = dynamic_cast<QToolButton *>(p_tb->widgetForAction(act));
    Q_ASSERT(toolBtn);
    toolBtn->setPopupMode(QToolButton::MenuButtonPopup);

    auto *menu = new QMenu(p_tb);
    menu->setToolTipsVisible(true);

    for (int level = 1; level <= 6; ++level) {
      auto *headingAct = menu->addAction(QObject::tr("Heading %1").arg(level));
      headingAct->setData(level);
      if (level != 1) {
        addActionShortcut(
            headingAct,
            editorConfig.getShortcut(static_cast<Shortcut>(Shortcut::TypeHeading1 + level - 1)),
            p_shortcutWidget, act);
      }
    }
    menu->addSeparator();
    {
      auto *noneAct = menu->addAction(QObject::tr("Clear"));
      noneAct->setData(0);
      addActionShortcut(noneAct, editorConfig.getShortcut(Shortcut::TypeHeadingNone),
                        p_shortcutWidget, act);
    }

    act->setMenu(menu);
    break;
  }

  case Action::TypeBold:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("type_bold_editor.svg")),
                          QObject::tr("Bold"));
    act->setProperty("iconName", QStringLiteral("type_bold_editor.svg"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeBold), p_shortcutWidget);
    break;

  case Action::TypeItalic:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("type_italic_editor.svg")),
                          QObject::tr("Italic"));
    act->setProperty("iconName", QStringLiteral("type_italic_editor.svg"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeItalic), p_shortcutWidget);
    break;

  case Action::TypeStrikethrough:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("type_strikethrough_editor.svg")),
                          QObject::tr("Strikethrough"));
    act->setProperty("iconName", QStringLiteral("type_strikethrough_editor.svg"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeStrikethrough), p_shortcutWidget);
    break;

  case Action::TypeMark:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("type_mark_editor.svg")),
                          QObject::tr("Mark"));
    act->setProperty("iconName", QStringLiteral("type_mark_editor.svg"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeMark), p_shortcutWidget);
    break;

  case Action::TypeUnorderedList:
    act =
        p_tb->addAction(generateIcon(p_services, QStringLiteral("type_unordered_list_editor.svg")),
                        QObject::tr("Unordered List"));
    act->setProperty("iconName", QStringLiteral("type_unordered_list_editor.svg"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeUnorderedList), p_shortcutWidget);
    break;

  case Action::TypeOrderedList:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("type_ordered_list_editor.svg")),
                          QObject::tr("Ordered List"));
    act->setProperty("iconName", QStringLiteral("type_ordered_list_editor.svg"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeOrderedList), p_shortcutWidget);
    break;

  case Action::TypeTodoList:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("type_todo_list_editor.svg")),
                          QObject::tr("Todo List"));
    act->setProperty("iconName", QStringLiteral("type_todo_list_editor.svg"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeTodoList), p_shortcutWidget);
    break;

  case Action::TypeCheckedTodoList:
    act = p_tb->addAction(
        generateIcon(p_services, QStringLiteral("type_checked_todo_list_editor.svg")),
        QObject::tr("Checked Todo List"));
    act->setProperty("iconName", QStringLiteral("type_checked_todo_list_editor.svg"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeCheckedTodoList),
                      p_shortcutWidget);
    break;

  case Action::TypeCode:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("type_code_editor.svg")),
                          QObject::tr("Code"));
    act->setProperty("iconName", QStringLiteral("type_code_editor.svg"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeCode), p_shortcutWidget);
    break;

  case Action::TypeCodeBlock:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("type_code_block_editor.svg")),
                          QObject::tr("Code Block"));
    act->setProperty("iconName", QStringLiteral("type_code_block_editor.svg"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeCodeBlock), p_shortcutWidget);
    break;

  case Action::TypeMath:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("type_math_editor.svg")),
                          QObject::tr("Math"));
    act->setProperty("iconName", QStringLiteral("type_math_editor.svg"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeMath), p_shortcutWidget);
    break;

  case Action::TypeMathBlock:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("type_math_block_editor.svg")),
                          QObject::tr("Math Block"));
    act->setProperty("iconName", QStringLiteral("type_math_block_editor.svg"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeMathBlock), p_shortcutWidget);
    break;

  case Action::TypeQuote:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("type_quote_editor.svg")),
                          QObject::tr("Quote"));
    act->setProperty("iconName", QStringLiteral("type_quote_editor.svg"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeQuote), p_shortcutWidget);
    break;

  case Action::TypeLink:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("type_link_editor.svg")),
                          QObject::tr("Link"));
    act->setProperty("iconName", QStringLiteral("type_link_editor.svg"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeLink), p_shortcutWidget);
    break;

  case Action::TypeImage:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("type_image_editor.svg")),
                          QObject::tr("Image"));
    act->setProperty("iconName", QStringLiteral("type_image_editor.svg"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeImage), p_shortcutWidget);
    break;

  case Action::TypeTable:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("type_table_editor.svg")),
                          QObject::tr("Table"));
    act->setProperty("iconName", QStringLiteral("type_table_editor.svg"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeTable), p_shortcutWidget);
    break;

  default:
    Q_ASSERT(false);
    break;
  }
  return act;
}
