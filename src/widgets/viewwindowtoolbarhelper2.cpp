#include "viewwindowtoolbarhelper2.h"

#include <QAction>
#include <QDebug>
#include <QMenu>
#include <QShortcut>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>

#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/servicelocator.h>
#include <gui/utils/widgetutils.h>

#include "attachmentpopup2.h"
#include "editreaddiscardaction.h"
#include "outlinepopup.h"
#include "propertydefs.h"
#include "tagpopup2.h"
#include "zoomtoolbar.h"

using namespace vnotex;

typedef EditorConfig::Shortcut Shortcut;

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
    act->setText(QStringLiteral("%1\t%2").arg(act->text(),
                                              shortcut->key().toString(QKeySequence::NativeText)));
  } else {
    p_btn->setText(QStringLiteral("%1\t%2").arg(
        p_btn->text(), shortcut->key().toString(QKeySequence::NativeText)));
  }
}

ZoomToolBar *ViewWindowToolBarHelper2::addZoomToolBar(QToolBar *p_tb, ServiceLocator &p_services) {
  return new ZoomToolBar(generateIcon(p_services, QStringLiteral("zoom_out_editor.svg")),
                         generateIcon(p_services, QStringLiteral("zoom_in_editor.svg")), p_tb);
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
    act->setProperty("iconName", QStringLiteral("save_editor.svg"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::Save), p_shortcutWidget);
    break;

  case Action::FindAndReplace:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("find_replace_editor.svg")),
                          QObject::tr("Find And Replace"));
    act->setProperty("iconName", QStringLiteral("find_replace_editor.svg"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::FindAndReplace), p_shortcutWidget);
    break;

  case Action::WordCount: {
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("word_count_editor.svg")),
                          QObject::tr("Word Count"));
    act->setProperty("iconName", QStringLiteral("word_count_editor.svg"));
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
    erdAct->setProperty("editIconName", QStringLiteral("edit_editor.svg"));
    erdAct->setProperty("readIconName", QStringLiteral("read_editor.svg"));
    erdAct->setProperty("discardIconName", QStringLiteral("discard_editor.svg"));
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

  case Action::TypeHeading:
  case Action::TypeBold:
  case Action::TypeItalic:
  case Action::TypeStrikethrough:
  case Action::TypeMark:
  case Action::TypeUnorderedList:
  case Action::TypeOrderedList:
  case Action::TypeTodoList:
  case Action::TypeCheckedTodoList:
  case Action::TypeCode:
  case Action::TypeCodeBlock:
  case Action::TypeMath:
  case Action::TypeMathBlock:
  case Action::TypeQuote:
  case Action::TypeLink:
  case Action::TypeImage:
  case Action::TypeTable:
    return addFormattingAction(p_tb, p_action, p_services, p_shortcutWidget);

  case Action::MindMap:
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("brain_editor.svg")),
                          QObject::tr("Mind Map"));
    act->setObjectName(QStringLiteral("outlineMindMapAction"));
    act->setProperty("iconName", QStringLiteral("brain_editor.svg"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::MindMap), p_shortcutWidget);
    break;

  case Action::Outline: {
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("outline_editor.svg")),
                          QObject::tr("Outline"));
    act->setProperty("iconName", QStringLiteral("outline_editor.svg"));
    auto *toolBtn = dynamic_cast<QToolButton *>(p_tb->widgetForAction(act));
    Q_ASSERT(toolBtn);
    toolBtn->setPopupMode(QToolButton::InstantPopup);
    toolBtn->setProperty(PropertyDefs::c_toolButtonWithoutMenuIndicator, true);
    addButtonShortcut(toolBtn, editorConfig.getShortcut(Shortcut::Outline), p_shortcutWidget);
    auto *menu = new OutlinePopup(p_services, toolBtn, p_tb);
    toolBtn->setMenu(menu);
    break;
  }

  case Action::Tag: {
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("tag_editor.svg")),
                          QObject::tr("Tags"));
    act->setProperty("iconName", QStringLiteral("tag_editor.svg"));
    auto *toolBtn = dynamic_cast<QToolButton *>(p_tb->widgetForAction(act));
    Q_ASSERT(toolBtn);
    toolBtn->setPopupMode(QToolButton::InstantPopup);
    toolBtn->setProperty(PropertyDefs::c_toolButtonWithoutMenuIndicator, true);
    addButtonShortcut(toolBtn, editorConfig.getShortcut(Shortcut::Tag), p_shortcutWidget);
    auto *menu = new TagPopup2(p_services, toolBtn, p_tb);
    toolBtn->setMenu(menu);
    break;
  }

  case Action::Attachment: {
    act = p_tb->addAction(generateIcon(p_services, QStringLiteral("attachment_editor.svg")),
                          QObject::tr("Attachments"));
    act->setProperty("iconName", QStringLiteral("attachment_editor.svg"));
    auto *toolBtn = dynamic_cast<QToolButton *>(p_tb->widgetForAction(act));
    Q_ASSERT(toolBtn);
    toolBtn->setPopupMode(QToolButton::InstantPopup);
    toolBtn->setProperty(PropertyDefs::c_toolButtonWithoutMenuIndicator, true);
    addButtonShortcut(toolBtn, editorConfig.getShortcut(Shortcut::Attachment), p_shortcutWidget);
    auto *menu = new AttachmentPopup2(p_services, toolBtn, p_tb);
    toolBtn->setMenu(menu);
    break;
  }

  case Action::Menu: {
    act =
        p_tb->addAction(generateIcon(p_services, QStringLiteral("menu.svg")), QObject::tr("Menu"));
    act->setProperty("iconName", QStringLiteral("menu.svg"));
    act->setMenu(new QMenu(p_tb));
    auto *toolBtn = dynamic_cast<QToolButton *>(p_tb->widgetForAction(act));
    Q_ASSERT(toolBtn);
    toolBtn->setPopupMode(QToolButton::InstantPopup);
    toolBtn->setProperty(PropertyDefs::c_toolButtonWithoutMenuIndicator, true);
    break;
  }

  default:
    Q_ASSERT(false);
    break;
  }

  return act;
}

QAction *ViewWindowToolBarHelper2::addAction(QMenu *p_menu, Action p_action,
                                             ServiceLocator &p_services,
                                             QWidget *p_shortcutWidget) {
  auto *configMgr = p_services.get<ConfigMgr2>();
  Q_ASSERT(configMgr);
  const auto &editorConfig = configMgr->getEditorConfig();

  QAction *act = nullptr;
  switch (p_action) {
  case Action::Print:
    act = p_menu->addAction(generateIcon(p_services, QStringLiteral("print_editor.svg")),
                            QObject::tr("Print"));
    act->setProperty("iconName", QStringLiteral("print_editor.svg"));
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::Print), p_shortcutWidget);
    break;

  case Action::ToggleLayoutMode:
    act = p_menu->addAction(generateIcon(p_services, QStringLiteral("readable_width_editor.svg")),
                            QObject::tr("Readable Width"));
    act->setProperty("iconName", QStringLiteral("readable_width_editor.svg"));
    act->setCheckable(true);
    {
      const auto &shortcut = editorConfig.getShortcut(Shortcut::ToggleLayoutMode);
      if (!shortcut.isEmpty()) {
        addActionShortcut(act, shortcut, p_shortcutWidget);
      }
    }
    break;

  case Action::ToggleLivePreview:
    act = p_menu->addAction(generateIcon(p_services, QStringLiteral("view_mode_editor.svg")),
                            QObject::tr("Live Preview"));
    act->setProperty("iconName", QStringLiteral("view_mode_editor.svg"));
    act->setCheckable(true);
    {
      const auto &shortcut = editorConfig.getShortcut(Shortcut::AlternateViewMode);
      if (!shortcut.isEmpty()) {
        addActionShortcut(act, shortcut, p_shortcutWidget);
      }
    }
    break;

  case Action::InplacePreview:
    act = p_menu->addAction(generateIcon(p_services, QStringLiteral("inplace_preview_editor.svg")),
                            QObject::tr("In-Place Preview"));
    act->setProperty("iconName", QStringLiteral("inplace_preview_editor.svg"));
    act->setCheckable(true);
    break;

  case Action::Concealment:
    act = p_menu->addAction(QObject::tr("Markers Concealment"));
    act->setObjectName(QStringLiteral("concealmentAction"));
    act->setMenu(new QMenu(p_menu));
    break;

  case Action::AllowAutoSectionNumber:
    act = p_menu->addAction(generateIcon(p_services, QStringLiteral("section_editor.svg")),
                            QObject::tr("Allow Auto Section Number"));
    act->setProperty("iconName", QStringLiteral("section_editor.svg"));
    act->setCheckable(true);
    break;

  case Action::ImageHost:
    act = p_menu->addAction(generateIcon(p_services, QStringLiteral("image_host_editor.svg")),
                            QObject::tr("Image Host"));
    act->setProperty("iconName", QStringLiteral("image_host_editor.svg"));
    act->setMenu(new QMenu(p_menu));
    break;

  case Action::Debug:
    act = p_menu->addAction(generateIcon(p_services, QStringLiteral("console_dock.svg")),
                            QObject::tr("Debug"));
    act->setProperty("iconName", QStringLiteral("console_dock.svg"));
    act->setCheckable(true);
    addActionShortcut(act, editorConfig.getShortcut(Shortcut::Debug), p_shortcutWidget);
    break;

  default:
    Q_ASSERT(false);
    break;
  }

  return act;
}

static void refreshActionIcons(const QList<QAction *> &p_actions, ServiceLocator &p_services) {
  for (auto *act : p_actions) {
    auto editIconName = act->property("editIconName").toString();
    if (!editIconName.isEmpty()) {
      auto *erdAct = dynamic_cast<EditReadDiscardAction *>(act);
      if (erdAct) {
        auto readIconName = act->property("readIconName").toString();
        auto discardIconName = act->property("discardIconName").toString();
        erdAct->refreshStateIcons(ViewWindowToolBarHelper2::generateIcon(p_services, editIconName),
                                  ViewWindowToolBarHelper2::generateIcon(p_services, readIconName));
        erdAct->refreshDiscardIcon(
            ViewWindowToolBarHelper2::generateIcon(p_services, discardIconName));
      }
    } else {
      auto iconName = act->property("iconName").toString();
      if (!iconName.isEmpty()) {
        act->setIcon(ViewWindowToolBarHelper2::generateIcon(p_services, iconName));
      }
    }
    if (auto *menu = act->menu()) {
      refreshActionIcons(menu->actions(), p_services);
    }
  }
}

void ViewWindowToolBarHelper2::refreshToolBarIcons(QToolBar *p_tb, ServiceLocator &p_services) {
  refreshActionIcons(p_tb->actions(), p_services);
}
