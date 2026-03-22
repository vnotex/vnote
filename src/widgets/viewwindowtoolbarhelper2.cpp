#include "viewwindowtoolbarhelper2.h"

#include <QAction>
#include <QDebug>
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

  case Action::EditRead:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("edit_editor.svg")),
                          QObject::tr("Edit"));
    act->setCheckable(true);
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::EditRead), p_shortcutWidget);
    break;

  default:
    Q_ASSERT(false);
    break;
  }

  return act;
}
