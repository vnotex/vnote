#include "mindmapviewwindow2.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QGraphicsView>
#include <QKeySequence>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QPointer>
#include <QPrinter>
#include <QScopeGuard>
#include <QSignalBlocker>
#include <QToolBar>
#include <QToolButton>

#include <utility>

#include <vxcore/vxcore.h>

#include <QShortcut>

#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/editorconfig.h>
#include <core/mindmapeditorconfig.h>
#include <core/servicelocator.h>
#include <core/sessionconfig.h>
#include <gui/services/themeservice.h>
#include <gui/utils/printutils.h>
#include <gui/utils/widgetutils.h>

#include "editors/mindmapeditor.h"
#include "editors/statuswidget.h"
#include "inlinebanner.h"
#include "outlinepopup.h"
#include "outlineprovider.h"
#include "presentationtoolbareffect.h"
#include "propertydefs.h"
#include "viewwindowtoolbarhelper2.h"
#include "zoomtoolbar.h"

using namespace vnotex;

MindMapViewWindow2::MindMapViewWindow2(ServiceLocator &p_services, const Buffer2 &p_buffer,
                                       QWidget *p_parent)
    : ViewWindow2(p_services, p_buffer, p_parent) {
  m_mode = ViewWindowMode::Edit;
  setupOutlineProvider();
  setupUI();
}

MindMapViewWindow2::~MindMapViewWindow2() {
  disconnect(m_toolsPanelFocusConn);
  setViewFullScreen(false);
}

bool MindMapViewWindow2::IsToolsPanelSupported() const {
  return m_editor && m_editor->nodePropertiesPanel();
}

QString MindMapViewWindow2::getToolsPanelTitle() const {
  return QCoreApplication::translate("m3::qt::NodePropertiesPanel", "Node Properties");
}

QWidget *MindMapViewWindow2::getToolsPanel() {
  QPointer<QWidget> panel = m_editor ? m_editor->nodePropertiesPanel() : nullptr;
  // Only the first handoff moves it out of the editor. An already-hosted panel
  // must stay in ToolsPanelHost, whose same-panel binding is intentionally a no-op.
  if (panel && panel->parentWidget() == m_editor) {
    panel->setParent(this);
  }
  return panel.data();
}

void MindMapViewWindow2::setupUI() {
  m_editor = new MindMapEditor(getServices(), getBuffer(), this);
  setCentralWidget(m_editor);
  connectEditorSignals();
  setupToolBar();
  // Find results and native diagnostics use the existing ViewWindow2 message surface.
  setStatusWidget(QSharedPointer<StatusWidget>::create());

  m_loadErrorBanner = new InlineBanner(InlineBanner::Severity::Error, QString(), this);
  addTopWidget(m_loadErrorBanner);
  m_loadErrorBanner->hide();
  applyEditorPalette();
  setupShortcuts();
  syncEditorFromBuffer();
}

void MindMapViewWindow2::connectEditorSignals() {
  connect(m_editor, &m3::qt::MindMapEditor::nodePropertiesPanelAboutToChange, this,
          &ViewWindow2::toolsPanelAboutToChange);
  connect(m_editor, &m3::qt::MindMapEditor::nodePropertiesPanelChanged, this,
          &ViewWindow2::toolsPanelChanged);
  connect(
      this, &ViewWindow2::detachedChanged, this,
      [this](bool p_detached) {
        const auto &config =
            getServices().get<ConfigMgr2>()->getEditorConfig().getMindMapEditorConfig();
        m_editor->setSeparateNodePropertiesPanel(!p_detached &&
                                                 config.getSeparateNodePropertiesPanel());
      },
      Qt::DirectConnection);
  m_toolsPanelFocusConn =
      connect(qApp, &QApplication::focusChanged, this, [this](QWidget *p_old, QWidget *p_now) {
        auto *panel = m_editor->nodePropertiesPanel();
        if (!panel || isAncestorOf(panel)) {
          return;
        }
        const auto inView = [this](QWidget *p_widget) {
          return p_widget && (p_widget == this || isAncestorOf(p_widget));
        };
        // Canvas transitions already run the base focus hooks. Cover only the
        // external controls which the ViewWindow's QWidget ancestry cannot see.
        if (inView(p_old) || inView(p_now)) {
          return;
        }
        const auto inPanel = [panel](QWidget *p_widget) {
          return p_widget && (p_widget == panel || panel->isAncestorOf(p_widget));
        };
        const bool hadFocus = inPanel(p_old);
        const bool hasFocus = inPanel(p_now);
        if (hasFocus && !hadFocus) {
          onFocusGained();
        } else if (!hasFocus && hadFocus) {
          onFocusLost();
        }
      });
  connect(m_editor, &MindMapEditor::contentsChanged, this, [this]() {
    if (m_propagateEditorToBuffer) {
      onEditorContentsChanged();
    }
  });
  connect(m_editor, &MindMapEditor::statusMessageRequested, this, [this](const QString &p_message) {
    m_lastEditorError = p_message;
    showMessage(p_message);
  });
  connect(m_editor, &m3::qt::MindMapEditor::documentChanged, this, [this]() {
    if (m_propagateEditorToBuffer && m_contentLoaded) {
      refreshOutline();
    }
  });
  connect(m_editor, &m3::qt::MindMapEditor::selectionChanged, this,
          [this](const QString &, const QString &) { updateCurrentHeading(); });
  connect(m_editor, &MindMapEditor::insertionAvailabilityChanged, this,
          &MindMapViewWindow2::syncInsertionActions);
  connect(m_editor, &m3::qt::MindMapEditor::backgroundContextMenuRequested, this,
          [this](QMenu *p_menu) {
            auto *action = new QAction(QObject::tr("E&xport"), p_menu);
            WidgetUtils::addActionShortcutText(
                action, getServices().get<ConfigMgr2>()->getCoreConfig().getShortcut(
                            CoreConfig::Shortcut::Export));
            action->setEnabled(m_contentLoaded);
            const auto generation = m_generation;
            connect(action, &QAction::triggered, this, [this, generation]() {
              if (generation == m_generation && m_contentLoaded) {
                emit exportRequested();
              }
            });
            p_menu->insertAction(m_editor->commandAction(QStringLiteral("showHelp")), action);
          });
}

void MindMapViewWindow2::setupToolBar() {
  auto *toolBar = createToolBar(this);
  addToolBar(toolBar);
  addLeftCommonToolBarActions(toolBar);
  toolBar->addSeparator();
  // The editor retains command ownership, shortcuts and history availability.
  for (const auto &name : {QStringLiteral("undo"), QStringLiteral("redo")}) {
    auto *action = m_editor->commandAction(name);
    const auto iconName = name + QStringLiteral("_editor.svg");
    action->setProperty("iconName", iconName);
    action->setIcon(ViewWindowToolBarHelper2::generateIcon(getServices(), iconName));
    toolBar->addAction(action);
  }
  toolBar->addSeparator();
  toolBar->addAction(m_editor->commandAction(QStringLiteral("fontSize")));
  const struct {
    const char *name;
    const char *icon;
  } formattingActions[] = {
      {"toggleBold", "type_bold_editor.svg"},      {"toggleItalic", "type_italic_editor.svg"},
      {"resetStyle", "reset_editor.svg"},          {"textColorPopup", "text_color_editor.svg"},
      {"fillColorPopup", "fill_color_editor.svg"}, {"iconsPopup", "icons_editor.svg"}};
  for (const auto &entry : formattingActions) {
    auto *action = m_editor->commandAction(QString::fromLatin1(entry.name));
    const auto iconName = QString::fromLatin1(entry.icon);
    action->setProperty("iconName", iconName);
    action->setIcon(ViewWindowToolBarHelper2::generateIcon(getServices(), iconName));
    toolBar->addAction(action);
    if (action->menu()) {
      auto *button = qobject_cast<QToolButton *>(toolBar->widgetForAction(action));
      if (button) {
        button->setPopupMode(QToolButton::InstantPopup);
        button->setProperty(PropertyDefs::c_toolButtonWithoutMenuIndicator, true);
      }
    }
  }
  toolBar->addSeparator();
  const auto insertionAction = [this, toolBar](const QString &p_label, const QString &p_icon) {
    auto *action =
        new QAction(ViewWindowToolBarHelper2::generateIcon(getServices(), p_icon), p_label, this);
    action->setToolTip(p_label);
    action->setProperty("iconName", p_icon);
    toolBar->addAction(action);
    return action;
  };
  m_insertLinkAction = insertionAction(tr("Insert Link"), QStringLiteral("type_link_editor.svg"));
  m_insertImageAction =
      insertionAction(tr("Insert Image"), QStringLiteral("type_image_editor.svg"));
  connect(m_insertLinkAction, &QAction::triggered, m_editor, &MindMapEditor::typeLink);
  connect(m_insertImageAction, &QAction::triggered, m_editor, &MindMapEditor::typeImage);
  refreshInsertionShortcuts();
  syncInsertionActions();
  addRightCommonToolBarActions(toolBar);
}

void MindMapViewWindow2::syncInsertionActions() {
  const bool available = m_contentLoaded && m_editor->canInsert();
  // The Tools panel may be reparented outside the disabled editor.
  if (auto *panel = m_editor->nodePropertiesPanel()) {
    panel->setEnabled(m_contentLoaded && m_editor->isEnabled());
    if (panel->parentWidget() != m_editor) {
      panel->setVisible(m_contentLoaded);
    }
  }
  if (m_insertLinkAction) {
    m_insertLinkAction->setEnabled(available);
    m_insertImageAction->setEnabled(available);
  }
}

void MindMapViewWindow2::refreshInsertionShortcuts() {
  qDeleteAll(m_insertionShortcuts);
  m_insertionShortcuts.clear();
  if (!m_insertLinkAction) {
    return;
  }
  m_insertLinkAction->setText(tr("Insert Link"));
  m_insertImageAction->setText(tr("Insert Image"));
  const auto &config = getServices().get<ConfigMgr2>()->getEditorConfig();
  const auto bind = [this, &config](QAction *p_action, EditorConfig::Shortcut p_shortcut) {
    // The shared helper parents its optional QShortcut to the shortcut widget.
    // Retain only the children it creates so a live config refresh cannot duplicate bindings.
    const int previousChildren = children().size();
    ViewWindowToolBarHelper2::addActionShortcut(p_action, config.getShortcut(p_shortcut), this);
    for (int index = previousChildren; index < children().size(); ++index) {
      if (auto *shortcut = qobject_cast<QShortcut *>(children().at(index))) {
        m_insertionShortcuts.append(shortcut);
      }
    }
  };
  bind(m_insertLinkAction, EditorConfig::Shortcut::TypeLink);
  bind(m_insertImageAction, EditorConfig::Shortcut::TypeImage);
}

void MindMapViewWindow2::addAdditionalRightToolBarActions(QToolBar *p_toolBar) {
  auto *outlineAction = addAction(p_toolBar, ViewWindowToolBarHelper2::Outline);
  auto *button = qobject_cast<QToolButton *>(p_toolBar->widgetForAction(outlineAction));
  if (button) {
    if (auto *popup = qobject_cast<OutlinePopup *>(button->menu())) {
      popup->setOutlineProvider(m_outlineProvider);
    }
  }
}

void MindMapViewWindow2::setupLayoutMenu(QToolBar *p_toolBar) {
  using LayoutDirection = m3::qt::MindMapEditor::LayoutDirection;
  const auto iconName = QStringLiteral("layout_dashboard_editor.svg");
  m_layoutAction = p_toolBar->addAction(
      ViewWindowToolBarHelper2::generateIcon(getServices(), iconName), tr("Layout"));
  m_layoutAction->setObjectName(QStringLiteral("mindMapLayoutAction"));
  m_layoutAction->setProperty("iconName", iconName);
  auto *menu = new QMenu(p_toolBar);
  menu->setObjectName(QStringLiteral("mindMapLayoutMenu"));
  m_layoutAction->setMenu(menu);
  m_layoutActionGroup = new QActionGroup(menu);
  m_layoutActionGroup->setExclusive(true);
  const auto addMode = [this, menu](const QString &p_label, LayoutDirection p_direction) {
    auto *action = menu->addAction(p_label);
    action->setCheckable(true);
    action->setData(static_cast<int>(p_direction));
    m_layoutActionGroup->addAction(action);
  };
  addMode(tr("Balanced"), LayoutDirection::Balanced);
  addMode(tr("Right"), LayoutDirection::Right);
  addMode(tr("Left"), LayoutDirection::Left);
  addMode(tr("Outline"), LayoutDirection::Outline);
  connect(m_layoutActionGroup, &QActionGroup::triggered, this, [this](QAction *p_action) {
    const auto direction = static_cast<LayoutDirection>(p_action->data().toInt());
    if (!m_contentLoaded || !m_editor->isEnabled()) {
      syncLayoutControls();
      return;
    }
    const QPointer<MindMapViewWindow2> guard(this);
    const auto generation = m_generation;
    bool applied = direction == m_editor->layoutDirection();
    if (!applied) {
      const bool committed = m_editor->commitActiveEdit();
      if (!guard) {
        return;
      }
      if (committed && generation == m_generation && m_contentLoaded && m_editor->isEnabled()) {
        applied = m_editor->setLayoutDirection(direction);
      }
    }
    if (!guard) {
      return;
    }
    if (applied && generation == m_generation && m_contentLoaded && m_editor->isEnabled() &&
        m_editor->layoutDirection() == direction) {
      using DefaultLayout = MindMapEditorConfig::LayoutDirection;
      auto defaultLayout = DefaultLayout::Balanced;
      switch (direction) {
      case LayoutDirection::Right:
        defaultLayout = DefaultLayout::Right;
        break;
      case LayoutDirection::Left:
        defaultLayout = DefaultLayout::Left;
        break;
      case LayoutDirection::Outline:
        defaultLayout = DefaultLayout::Outline;
        break;
      default:
        break;
      }
      getServices()
          .get<ConfigMgr2>()
          ->getEditorConfig()
          .getMindMapEditorConfig()
          .setDefaultLayoutDirection(defaultLayout);
    }
    if (guard) {
      syncLayoutControls();
      if (applied && generation == m_generation && m_contentLoaded && m_editor->isEnabled() &&
          m_editor->layoutDirection() == direction) {
        if (auto *view = m_editor->findChild<QGraphicsView *>()) {
          view->setFocus(Qt::OtherFocusReason);
        }
      }
    }
  });
  connect(m_editor, &m3::qt::MindMapEditor::layoutDirectionChanged, this,
          &MindMapViewWindow2::syncLayoutControls);
  connect(menu, &QMenu::aboutToShow, this, &MindMapViewWindow2::syncLayoutControls);
  if (auto *button = qobject_cast<QToolButton *>(p_toolBar->widgetForAction(m_layoutAction))) {
    button->setPopupMode(QToolButton::InstantPopup);
    button->setProperty(PropertyDefs::c_toolButtonWithoutMenuIndicator, true);
  }
  syncLayoutControls();
}

void MindMapViewWindow2::syncLayoutControls() {
  if (!m_layoutAction) {
    return;
  }
  const bool available = m_contentLoaded && m_editor->isEnabled();
  m_layoutAction->setEnabled(available);
  m_layoutActionGroup->setEnabled(available);
  const int direction = static_cast<int>(m_editor->layoutDirection());
  for (auto *action : m_layoutActionGroup->actions()) {
    // Let QActionGroup observe changed(); setChecked() does not emit triggered().
    action->setChecked(action->data().toInt() == direction);
  }
}

void MindMapViewWindow2::addAdditionalViewToolBarActions(QToolBar *p_toolBar) {
  const auto iconName = QStringLiteral("presentation_editor.svg");
  m_presentationAction = p_toolBar->addAction(
      ViewWindowToolBarHelper2::generateIcon(getServices(), iconName), tr("Presentation Mode"));
  m_presentationAction->setProperty("iconName", iconName);
  ViewWindowToolBarHelper2::addActionShortcut(
      m_presentationAction,
      getServices().get<ConfigMgr2>()->getEditorConfig().getShortcut(
          EditorConfig::Shortcut::PresentationMode),
      this);
  m_presentationAction->setCheckable(true);
  m_presentationEffect = new PresentationToolBarEffect(p_toolBar, this);

  connect(m_presentationAction, &QAction::triggered, this, [this]() {
    setViewFullScreen(!isViewFullScreen());
    const QSignalBlocker blocker(m_presentationAction);
    m_presentationAction->setChecked(isViewFullScreen());
  });
  connect(this, &ViewWindow2::viewFullScreenExitRequested, this,
          [this]() { setViewFullScreen(false); });
  connect(this, &ViewWindow2::viewFullScreenChanged, this, [this](bool p_on) {
    const QSignalBlocker blocker(m_presentationAction);
    m_presentationAction->setChecked(p_on);
    m_presentationEffect->setActive(p_on);
    update();
    if (p_on) {
      m_editor->collapseNodeProperties();
    }
  });

  setupLayoutMenu(p_toolBar);
  p_toolBar->addSeparator();
  m_zoomToolBar = ViewWindowToolBarHelper2::addZoomToolBar(p_toolBar, getServices());
  m_zoomToolBar->setShortcutTips(QKeySequence(Qt::CTRL | Qt::Key_Equal),
                                 QKeySequence(Qt::CTRL | Qt::Key_Minus));
  connect(m_zoomToolBar, &ZoomToolBar::zoomRequested, this, &MindMapViewWindow2::zoom);
  connect(m_zoomToolBar, &ZoomToolBar::zoomSelected, this, [this](qreal p_zoom) {
    if (m_contentLoaded) {
      applyZoom(p_zoom);
      syncZoomControls();
    }
  });
  connect(m_editor, &m3::qt::MindMapEditor::zoomChanged, this, [this]() {
    if (m_contentLoaded) {
      getServices().get<ConfigMgr2>()->getSessionConfig().setMindMapZoomFactor(zoomChoice());
    }
    syncZoomControls();
  });
  syncZoomControls();
}

qreal MindMapViewWindow2::zoomChoice() const {
  using ZoomMode = m3::qt::MindMapEditor::ZoomMode;
  const auto mode = m_editor->zoomMode();
  return mode == ZoomMode::Width ? -1.0 : mode == ZoomMode::Page ? 0.0 : m_editor->zoomFactor();
}

void MindMapViewWindow2::applyZoom(qreal p_zoom) {
  if (p_zoom == -1.0) {
    m_editor->fitToWidth();
  } else if (p_zoom == 0.0) {
    m_editor->fitToContents();
  } else {
    m_editor->zoom(p_zoom / m_editor->zoomFactor());
  }
}

void MindMapViewWindow2::syncZoomControls() {
  m_zoomToolBar->setEnabled(m_contentLoaded);
  m_zoomToolBar->setZoom(zoomChoice());
}

void MindMapViewWindow2::releaseBufferResources() {
  ++m_generation;
  m_propagateEditorToBuffer = false;
  m_contentLoaded = false;
  const QPointer<MindMapViewWindow2> guard(this);
  // Revoke insertion dialogs, image overlays, image requests and link consent
  // while the base still owns the outgoing identity. No native widget is rebuilt.
  m_editor->setBuffer(Buffer2());
  if (!guard) {
    return;
  }
  m_editor->clearFind();
  if (!guard) {
    return;
  }
  syncZoomControls();
  syncLayoutControls();
  syncInsertionActions();
  clearOutline();
}

void MindMapViewWindow2::bindBufferResources() {
  m_lastEditorError.clear();
  m_loadErrorBanner->hide();
  m_propagateEditorToBuffer = true;
  // syncEditorFromBuffer() binds the incoming buffer and unconditionally loads
  // its native document, resetting history, selection, search and camera.
}

void MindMapViewWindow2::syncEditorFromBuffer() {
  const QPointer<MindMapViewWindow2> guard(this);
  const auto generation = ++m_generation;
  const bool propagate = m_propagateEditorToBuffer;
  m_propagateEditorToBuffer = false;
  const auto restorePropagation = qScopeGuard([guard, propagate]() {
    if (guard) {
      guard->m_propagateEditorToBuffer = propagate;
    }
  });
  m_contentLoaded = false;
  syncZoomControls();
  syncLayoutControls();
  syncInsertionActions();
  m_lastEditorError.clear();
  clearOutline();
  if (!guard || generation != m_generation) {
    return;
  }

  // Keep raw bytes: an invalid nonempty document must never turn into an empty new map.
  const Buffer2 buffer = getBuffer();
  m_editor->setBuffer(buffer);
  if (!guard || generation != m_generation) {
    return;
  }
  if (buffer.isValid()) {
    VxCoreError error = VXCORE_OK;
    const QByteArray content = buffer.getContentRaw(&error);
    if (error == VXCORE_OK) {
      const bool loaded = m_editor->loadContent(content);
      if (!guard || generation != m_generation) {
        return;
      }
      if (loaded) {
        // Restore before accepting zoom signals: loading resets the native camera to 100%.
        applyZoom(getServices().get<ConfigMgr2>()->getSessionConfig().getMindMapZoomFactor());
      }
      m_contentLoaded = loaded;
    } else {
      // Detach the unavailable source rather than feeding fabricated/empty content to m3.
      m_editor->setBuffer(Buffer2());
      if (!guard || generation != m_generation) {
        return;
      }
      m_lastEditorError =
          tr("Unable to read the mind map: %1").arg(QString::fromUtf8(vxcore_error_message(error)));
    }
  }

  m_editor->setModified(buffer.isValid() && buffer.isModified());
  m_lastKnownRevision = buffer.isValid() ? buffer.getRevision() : 0;
  m_propagateEditorToBuffer = propagate;
  syncZoomControls();
  syncLayoutControls();
  syncInsertionActions();
  if (m_contentLoaded) {
    m_loadErrorBanner->hide();
    refreshOutline();
  } else {
    const auto message =
        m_lastEditorError.isEmpty() ? tr("Unable to load the mind map.") : m_lastEditorError;
    m_loadErrorBanner->setText(message);
    m_loadErrorBanner->show();
    showMessage(message);
  }
}

void MindMapViewWindow2::handlePrint() {
  if (getBuffer().isEncrypted()) {
    showMessage(tr("Printing encrypted notes is not supported"));
    return;
  }
  if (m_printing || !m_contentLoaded || !isEnabled() || !m_editor->isEnabled() ||
      !getBuffer().isValid()) {
    return;
  }
  const QPointer<MindMapViewWindow2> guard(this);
  const auto generation = m_generation;
  setViewFullScreen(false);
  if (!guard) {
    return;
  }
  const auto printer = PrintUtils::promptForPrint(false, this);
  if (guard && generation == m_generation) {
    printDocument(printer);
  }
}

void MindMapViewWindow2::printDocument(const QSharedPointer<QPrinter> &p_printer) {
  if (!p_printer || m_printing || !m_contentLoaded || !isEnabled() || !m_editor->isEnabled() ||
      !getBuffer().isValid() || getBuffer().isEncrypted()) {
    return;
  }
  if (p_printer->printRange() == QPrinter::PageRange && p_printer->fromPage() > 1) {
    return;
  }
  const QPointer<MindMapViewWindow2> guard(this);
  const auto generation = m_generation;
  m_printing = true;
  const auto clearPrinting = qScopeGuard([guard] {
    if (guard) {
      guard->m_printing = false;
    }
  });
  const bool committed = m_editor->commitActiveEdit(true);
  if (!guard || generation != m_generation) {
    return;
  }
  if (!committed) {
    showMessage(tr("Failed to print"));
    return;
  }
  if (generation != m_generation || !m_contentLoaded || !isEnabled() || !m_editor->isEnabled() ||
      !getBuffer().isValid() || getBuffer().isEncrypted()) {
    return;
  }
  QPainter painter;
  const bool started = painter.begin(p_printer.get());
  // Native print drivers may present their own modal output dialog during begin().
  if (!guard || generation != m_generation) {
    p_printer->abort();
    return;
  }
  if (!started) {
    showMessage(tr("Failed to print"));
    return;
  }
  if (generation != m_generation || !m_contentLoaded || !isEnabled() || !m_editor->isEnabled() ||
      !getBuffer().isValid() || getBuffer().isEncrypted()) {
    p_printer->abort();
    return;
  }
  const bool rendered =
      m_editor->renderMap(painter, QRectF(0, 0, p_printer->width(), p_printer->height()));
  const bool finished = painter.end();
  const auto state = p_printer->printerState();
  if (guard && generation == m_generation) {
    showMessage(rendered && finished && state != QPrinter::Error && state != QPrinter::Aborted
                    ? tr("Printing completed")
                    : tr("Failed to print"));
  }
}

QString MindMapViewWindow2::getLatestContent() const { return m_editor->contentForSave(); }

QString MindMapViewWindow2::selectedText() const {
  return m_contentLoaded ? m_editor->selectedText() : QString();
}

void MindMapViewWindow2::setModified(bool p_modified) { m_editor->setModified(p_modified); }

void MindMapViewWindow2::setMode(ViewWindowMode p_mode) {
  Q_UNUSED(p_mode);
  m_mode = ViewWindowMode::Edit;
}

void MindMapViewWindow2::handleNodeRetargeted(const NodeIdentifier &p_newNodeId) {
  ViewWindow2::handleNodeRetargeted(p_newNodeId);
  ++m_generation;
  // Rename/move updates resource policy only; drafts, history and camera stay native-owned.
  m_editor->setBuffer(getBuffer());
}

void MindMapViewWindow2::handleEditorConfigChange() {
  const QPointer<MindMapViewWindow2> guard(this);
  ViewWindow2::handleEditorConfigChange();
  if (guard && m_editor) {
    m_editor->applyConfig(!isDetached());
  }
  if (guard) {
    refreshInsertionShortcuts();
    syncInsertionActions();
    applyEditorPalette();
  }
}

void MindMapViewWindow2::handleThemeChanged() {
  const QPointer<MindMapViewWindow2> guard(this);
  ViewWindow2::handleThemeChanged();
  if (guard) {
    applyEditorPalette();
  }
}

void MindMapViewWindow2::paintEvent(QPaintEvent *p_event) {
  ViewWindow2::paintEvent(p_event);
  if (isViewFullScreen()) {
    QPainter painter(this);
    painter.fillRect(rect(), m_presentationBackground);
  }
}

void MindMapViewWindow2::applyEditorPalette() {
  if (!m_editor) {
    return;
  }
  QPalette contentPalette = palette();
  if (auto *theme = getServices().get<ThemeService>()) {
    const QColor background(theme->paletteColor(QStringLiteral("base#content#bg")));
    const QColor foreground(theme->paletteColor(QStringLiteral("base#content#fg")));
    if (background.isValid()) {
      contentPalette.setColor(QPalette::Base, background);
      contentPalette.setColor(QPalette::Window, background);
      contentPalette.setColor(QPalette::Button, background);
    }
    if (foreground.isValid()) {
      contentPalette.setColor(QPalette::Text, foreground);
      contentPalette.setColor(QPalette::WindowText, foreground);
      contentPalette.setColor(QPalette::ButtonText, foreground);
    }
  }
  m_presentationBackground = contentPalette.color(QPalette::Base);
  if (isViewFullScreen()) {
    update();
  }
  const QPointer<MindMapViewWindow2> guard(this);
  m_editor->setPalette(contentPalette);
  if (guard) {
    m_editor->setFont(font());
  }
}

void MindMapViewWindow2::scrollUp() {
  if (m_contentLoaded) {
    m_editor->scrollSteps(0, -1);
  }
}

void MindMapViewWindow2::scrollDown() {
  if (m_contentLoaded) {
    m_editor->scrollSteps(0, 1);
  }
}

void MindMapViewWindow2::zoom(bool p_zoomIn) {
  if (m_contentLoaded) {
    m_editor->zoom(p_zoomIn ? 1.2 : 1.0 / 1.2);
  }
}

void MindMapViewWindow2::resetZoom() {
  if (m_contentLoaded) {
    m_editor->resetZoom();
  }
}

QSharedPointer<OutlineProvider> MindMapViewWindow2::getOutlineProvider() const {
  return m_outlineProvider;
}

void MindMapViewWindow2::setupOutlineProvider() {
  m_outlineProvider = QSharedPointer<OutlineProvider>::create();
  m_outlineProvider->setAutoSectionNumberAllowed(false);
  m_outlineProvider->setReorderSupported(false);
  connect(m_outlineProvider.data(), &OutlineProvider::headingClicked, this, [this](int p_index) {
    if (!m_contentLoaded || p_index < 0 || p_index >= m_outlineNodeIds.size()) {
      return;
    }
    // Copy across revealNode(): committing a draft can synchronously rebuild the outline.
    const QString nodeId = m_outlineNodeIds.at(p_index);
    m_editor->revealNode(nodeId);
  });
}

void MindMapViewWindow2::clearOutline() {
  m_outlineNodeIds.clear();
  m_outlineIndexes.clear();
  const QPointer<MindMapViewWindow2> guard(this);
  const auto generation = m_generation;
  m_outlineProvider->setCurrentHeadingIndex(-1);
  if (guard && generation == m_generation) {
    m_outlineProvider->setOutline(QSharedPointer<Outline>::create());
  }
}

void MindMapViewWindow2::refreshOutline() {
  if (!m_contentLoaded) {
    clearOutline();
    return;
  }
  const QPointer<MindMapViewWindow2> guard(this);
  const auto generation = m_generation;
  const auto entries = m_editor->outline();
  if (!guard || generation != m_generation) {
    return;
  }
  if (entries.isEmpty()) {
    const auto error = m_editor->lastError();
    clearOutline();
    if (guard && generation == m_generation) {
      showMessage(error.isEmpty() ? tr("Unable to read the mind map outline.") : error);
    }
    return;
  }
  auto outline = QSharedPointer<Outline>::create();
  outline->m_headings.reserve(entries.size());
  QStringList nodeIds;
  nodeIds.reserve(entries.size());
  QHash<QString, int> indexes;
  indexes.reserve(entries.size());
  for (const auto &entry : entries) {
    indexes.insert(entry.id, nodeIds.size());
    nodeIds.append(entry.id);
    outline->m_headings.append(Outline::Heading(entry.topic, entry.level));
  }
  m_outlineNodeIds = std::move(nodeIds);
  m_outlineIndexes = std::move(indexes);
  m_outlineProvider->setOutline(outline);
  if (guard && generation == m_generation) {
    updateCurrentHeading();
  }
}

void MindMapViewWindow2::updateCurrentHeading() {
  const auto id = m_contentLoaded ? m_editor->selectedNodeId() : QString();
  m_outlineProvider->setCurrentHeadingIndex(id.isEmpty() ? -1 : m_outlineIndexes.value(id, -1));
}

void MindMapViewWindow2::findText(const QString &p_text, FindOptions p_options,
                                  bool p_incremental) {
  const QPointer<MindMapViewWindow2> guard(this);
  const auto generation = m_generation;
  m3::qt::FindResult result;
  if (m_contentLoaded) {
    result = m_editor->findText(p_text,
                                p_options.testFlag(FindOption::CaseSensitive) ? Qt::CaseSensitive
                                                                              : Qt::CaseInsensitive,
                                p_options.testFlag(FindOption::FindBackward), p_incremental);
  }
  if (guard && generation == m_generation) {
    showFindResult(QStringList(p_text), result.totalMatches, result.currentMatch);
  }
}

void MindMapViewWindow2::handleFindTextChanged(const QString &p_text, FindOptions p_options) {
  if (p_text.isEmpty() || p_options.testFlag(FindOption::IncrementalSearch)) {
    findText(p_text, p_options, true);
  }
}

void MindMapViewWindow2::handleFindNext(const QStringList &p_texts, FindOptions p_options) {
  findText(p_texts.isEmpty() ? QString() : p_texts.constFirst(), p_options, false);
}

void MindMapViewWindow2::handleReplace(const QString &p_text, FindOptions p_options,
                                       const QString &p_replaceText) {
  Q_UNUSED(p_text);
  Q_UNUSED(p_options);
  Q_UNUSED(p_replaceText);
  showMessage(tr("Replacement is not supported for mind maps."));
}

void MindMapViewWindow2::handleReplaceAll(const QString &p_text, FindOptions p_options,
                                          const QString &p_replaceText) {
  handleReplace(p_text, p_options, p_replaceText);
}

void MindMapViewWindow2::clearHighlights() { m_editor->clearFind(); }

void MindMapViewWindow2::handleFindAndReplaceWidgetClosed() { clearHighlights(); }

void MindMapViewWindow2::handleFindAndReplaceWidgetOpened() {
  setFindAndReplaceReplaceEnabled(false);
  setFindAndReplaceOptionsEnabled(FindOption::WholeWordOnly | FindOption::RegularExpression, false);
}
