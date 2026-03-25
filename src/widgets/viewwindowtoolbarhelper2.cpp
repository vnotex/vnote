#include "viewwindowtoolbarhelper2.h"

#include <QAction>
#include <QDebug>
#include <QMenu>
#include <QShortcut>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>

#include <core/editorconfig.h>
#include <core/configmgr2.h>
#include <core/servicelocator.h>
#include <gui/services/themeservice.h>
#include <gui/utils/widgetutils.h>
#include <utils/iconutils.h>

#include "editreaddiscardaction.h"
#include "outlinepopup.h"
#include "propertydefs.h"

using namespace vnotex;

typedef EditorConfig::Shortcut Shortcut;

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

void ViewWindowToolBarHelper2::addButtonShortcut(QToolButton *p_btn, const QString &p_shortcut,
                                                 QWidget *p_widget) {
  auto *shortcut =
      WidgetUtils::createShortcut(p_shortcut, p_widget, Qt::WidgetWithChildrenShortcut);
  if (!shortcut) {
    return;
  }

  QObject::connect(shortcut, &QShortcut::activated, p_btn, [p_btn]() {
    if (p_btn->isEnabled()) {
      p_btn->click();
    }
  });
  auto *act = p_btn->defaultAction();
  if (act) {
    act->setText(QStringLiteral("%1\t%2").arg(
        act->text(), shortcut->key().toString(QKeySequence::NativeText)));
  } else {
    p_btn->setText(QStringLiteral("%1\t%2").arg(
        p_btn->text(), shortcut->key().toString(QKeySequence::NativeText)));
  }
}

QIcon ViewWindowToolBarHelper2::generateIcon(ServiceLocator &p_services,
                                             const QString &p_iconName) {
  static QVector<IconUtils::OverriddenColor> s_colors;

  auto *themeService = p_services.get<ThemeService>();
  if (!themeService) {
    return QIcon();
  }

  if (s_colors.isEmpty()) {
    const auto fg = themeService->paletteColor(QStringLiteral("widgets#toolbar#icon#fg"));
    const auto disabledFg =
        themeService->paletteColor(QStringLiteral("widgets#toolbar#icon#disabled#fg"));

    s_colors.push_back(IconUtils::OverriddenColor(fg, QIcon::Normal));
    s_colors.push_back(IconUtils::OverriddenColor(disabledFg, QIcon::Disabled));
  }

  auto iconFile = themeService->getIconFile(p_iconName);
  return IconUtils::fetchIcon(iconFile, s_colors);
}

void ViewWindowToolBarHelper2::addSpacer(QToolBar *p_tb) {
  auto *spacer = new QWidget(p_tb);
  spacer->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
  auto *act = p_tb->addWidget(spacer);
  act->setEnabled(false);
}

QAction *ViewWindowToolBarHelper2::addAction(QToolBar *p_tb, Action p_action,
                                             ServiceLocator &p_services,
                                             QWidget *p_shortcutWidget) {
  auto *configMgr = p_services.get<ConfigMgr2>();
  Q_ASSERT(configMgr);
  const auto &editorConfig = configMgr->getEditorConfig();

  QAction *act = nullptr;
  switch (p_action) {
  case Action::Save:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("save_editor.svg")),
                          QObject::tr("Save"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::Save), p_shortcutWidget);
    break;

  case Action::FindAndReplace:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("find_replace_editor.svg")),
                          QObject::tr("Find And Replace"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::FindAndReplace), p_shortcutWidget);
    break;

  case Action::Print:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("print_editor.svg")),
                          QObject::tr("Print"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::Print), p_shortcutWidget);
    break;

  case Action::WordCount: {
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("word_count_editor.svg")),
                          QObject::tr("Word Count"));
    auto *toolBtn = dynamic_cast<QToolButton *>(p_tb->widgetForAction(act));
    Q_ASSERT(toolBtn);
    toolBtn->setPopupMode(QToolButton::InstantPopup);
    toolBtn->setProperty(PropertyDefs::c_toolButtonWithoutMenuIndicator, true);
    addButtonShortcut(toolBtn, editorConfig.getShortcut(Shortcut::WordCount), p_shortcutWidget);
    break;
  }

  case Action::EditRead: {
    auto *erdAct = new EditReadDiscardAction(
        generateIcon(p_services, QStringLiteral("edit_editor.svg")), QObject::tr("Edit"),
        generateIcon(p_services, QStringLiteral("read_editor.svg")), QObject::tr("Read"),
        generateIcon(p_services, QStringLiteral("discard_editor.svg")), QObject::tr("Discard"),
        p_tb);
    act = erdAct;
    addActionShortcut(erdAct, editorConfig.getShortcut(Shortcut::EditRead), p_shortcutWidget);

    auto *discardAct = erdAct->getDiscardAction();
    addActionShortcut(discardAct, editorConfig.getShortcut(Shortcut::Discard), p_shortcutWidget);

    p_tb->addAction(erdAct);

    auto *toolBtn = dynamic_cast<QToolButton *>(p_tb->widgetForAction(erdAct));
    Q_ASSERT(toolBtn);
    erdAct->setToolButtonForAction(toolBtn);
    break;
  }

  case Action::TypeHeading: {
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("type_heading_editor.svg")),
                          QObject::tr("Heading 1"));
    act->setData(1);
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeHeading1), p_shortcutWidget);

    auto *toolBtn = dynamic_cast<QToolButton *>(p_tb->widgetForAction(act));
    Q_ASSERT(toolBtn);
    toolBtn->setPopupMode(QToolButton::MenuButtonPopup);

    auto *menu = new QMenu(toolBtn);
    menu->setToolTipsVisible(true);

    for (int level = 2; level <= 6; ++level) {
      auto *headingAct = menu->addAction(QObject::tr("Heading %1").arg(level));
      headingAct->setData(level);
      addActionShortcut(headingAct,
                        editorConfig.getShortcut(
                            static_cast<Shortcut>(Shortcut::TypeHeading1 + level - 1)),
                        p_shortcutWidget, act);
    }
    menu->addSeparator();
    {
      auto *noneAct = menu->addAction(QObject::tr("Clear"));
      noneAct->setData(0);
      addActionShortcut(noneAct, editorConfig.getShortcut(Shortcut::TypeHeadingNone),
                        p_shortcutWidget, act);
    }

    toolBtn->setMenu(menu);
    break;
  }

  case Action::TypeBold:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("type_bold_editor.svg")),
                          QObject::tr("Bold"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeBold), p_shortcutWidget);
    break;

  case Action::TypeItalic:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("type_italic_editor.svg")),
                          QObject::tr("Italic"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeItalic), p_shortcutWidget);
    break;

  case Action::TypeStrikethrough:
    act = p_tb->addAction(
        generateIcon(p_services, QStringLiteral("type_strikethrough_editor.svg")),
        QObject::tr("Strikethrough"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeStrikethrough),
                      p_shortcutWidget);
    break;

  case Action::TypeMark:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("type_mark_editor.svg")),
                          QObject::tr("Mark"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeMark), p_shortcutWidget);
    break;

  case Action::TypeUnorderedList:
    act = p_tb->addAction(
        generateIcon(p_services, QStringLiteral("type_unordered_list_editor.svg")),
        QObject::tr("Unordered List"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeUnorderedList),
                      p_shortcutWidget);
    break;

  case Action::TypeOrderedList:
    act = p_tb->addAction(
        generateIcon(p_services, QStringLiteral("type_ordered_list_editor.svg")),
        QObject::tr("Ordered List"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeOrderedList),
                      p_shortcutWidget);
    break;

  case Action::TypeTodoList:
    act = p_tb->addAction(
        generateIcon(p_services, QStringLiteral("type_todo_list_editor.svg")),
        QObject::tr("Todo List"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeTodoList), p_shortcutWidget);
    break;

  case Action::TypeCheckedTodoList:
    act = p_tb->addAction(
        generateIcon(p_services, QStringLiteral("type_checked_todo_list_editor.svg")),
        QObject::tr("Checked Todo List"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeCheckedTodoList),
                      p_shortcutWidget);
    break;

  case Action::TypeCode:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("type_code_editor.svg")),
                          QObject::tr("Code"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeCode), p_shortcutWidget);
    break;

  case Action::TypeCodeBlock:
    act = p_tb->addAction(
        generateIcon(p_services, QStringLiteral("type_code_block_editor.svg")),
        QObject::tr("Code Block"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeCodeBlock), p_shortcutWidget);
    break;

  case Action::TypeMath:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("type_math_editor.svg")),
                          QObject::tr("Math"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeMath), p_shortcutWidget);
    break;

  case Action::TypeMathBlock:
    act = p_tb->addAction(
        generateIcon(p_services, QStringLiteral("type_math_block_editor.svg")),
        QObject::tr("Math Block"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeMathBlock), p_shortcutWidget);
    break;

  case Action::TypeQuote:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("type_quote_editor.svg")),
                          QObject::tr("Quote"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeQuote), p_shortcutWidget);
    break;

  case Action::TypeLink:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("type_link_editor.svg")),
                          QObject::tr("Link"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeLink), p_shortcutWidget);
    break;

  case Action::TypeImage:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("type_image_editor.svg")),
                          QObject::tr("Image"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeImage), p_shortcutWidget);
    break;

  case Action::TypeTable:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("type_table_editor.svg")),
                          QObject::tr("Table"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::TypeTable), p_shortcutWidget);
    break;

  case Action::ToggleLayoutMode:
    act = p_tb->addAction(
        generateIcon(p_services, QStringLiteral("readable_width_editor.svg")),
        QObject::tr("Readable Width"));
    act->setCheckable(true);
    {
      const auto &shortcut = editorConfig.getShortcut(Shortcut::ToggleLayoutMode);
      if (!shortcut.isEmpty()) {
        addActionShortcut(act, shortcut, p_shortcutWidget);
      }
    }
    break;

  case Action::ToggleLivePreview:
    act = p_tb->addAction(
        generateIcon(p_services, QStringLiteral("view_mode_editor.svg")),
        QObject::tr("Live Preview"));
    act->setCheckable(true);
    {
      const auto &shortcut = editorConfig.getShortcut(Shortcut::AlternateViewMode);
      if (!shortcut.isEmpty()) {
        addActionShortcut(act, shortcut, p_shortcutWidget);
      }
    }
    break;

  case Action::Outline: {
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("outline_editor.svg")),
                          QObject::tr("Outline"));
    auto *toolBtn = dynamic_cast<QToolButton *>(p_tb->widgetForAction(act));
    Q_ASSERT(toolBtn);
    toolBtn->setPopupMode(QToolButton::InstantPopup);
    toolBtn->setProperty(PropertyDefs::c_toolButtonWithoutMenuIndicator, true);
    addButtonShortcut(toolBtn, editorConfig.getShortcut(Shortcut::Outline), p_shortcutWidget);
    auto *menu = new OutlinePopup(p_services, toolBtn, p_tb);
    toolBtn->setMenu(menu);
    break;
  }

  default:
    Q_ASSERT(false);
    break;
  }

  return act;
}
