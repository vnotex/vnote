#include "textviewwindow2.h"

#include <QAction>
#include <QPrinter>
#include <QScrollBar>
#include <QToolBar>

#include <vtextedit/vtextedit.h>

#include <controllers/textviewwindowcontroller.h>
#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/servicelocator.h>
#include <core/texteditorconfig.h>
#include <gui/services/themeservice.h>
#include <gui/utils/widgetutils.h>

#include "editors/statuswidget.h"
#include "editors/texteditor.h"
#include "findandreplacewidget2.h"
#include "textviewwindowhelper.h"
#include "viewwindowtoolbarhelper2.h"

#include <gui/utils/printutils.h>

using namespace vnotex;

TextViewWindow2::TextViewWindow2(ServiceLocator &p_services, const Buffer2 &p_buffer,
                                 QWidget *p_parent)
    : ViewWindow2(p_services, p_buffer, p_parent) {
  m_controller = new TextViewWindowController(p_services, this);
  m_mode = ViewWindowMode::Edit;
  setupUI();
}

void TextViewWindow2::setupUI() {
  auto *configMgr = getServices().get<ConfigMgr2>();
  const auto &editorConfig = configMgr->getEditorConfig();
  const auto &textEditorConfig = editorConfig.getTextEditorConfig();

  m_controller->checkAndUpdateConfigRevision();

  auto *themeService = getServices().get<ThemeService>();
  auto themeFile = themeService->getFile(Theme::File::TextEditorStyle);
  auto syntaxTheme = themeService->getEditorHighlightTheme();
  qreal scaleFactor = WidgetUtils::calculateScaleFactor();

  // Central widget: text editor.
  {
    m_editor = new TextEditor(
        TextViewWindowController::buildTextEditorConfig(
            editorConfig, textEditorConfig, themeFile, syntaxTheme, scaleFactor),
        TextViewWindowController::buildTextEditorParameters(editorConfig, textEditorConfig),
        this);
    setCentralWidget(m_editor);

    updateEditorFromConfig();
  }

  connectEditorSignals();

  // Status widget.
  {
    auto statusWidget = QSharedPointer<StatusWidget>::create();
    statusWidget->setEditorStatusWidget(m_editor->statusWidget());
    setStatusWidget(statusWidget);
  }

  // Toolbar.
  setupToolBar();

  // Initial sync from buffer.
  m_propagateEditorToBuffer = false;
  syncEditorFromBuffer();
  m_propagateEditorToBuffer = true;
}

void TextViewWindow2::setupToolBar() {
  auto *toolBar = createToolBar(this);
  addToolBar(toolBar);

  addAction(toolBar, ViewWindowToolBarHelper2::Save);
  addAction(toolBar, ViewWindowToolBarHelper2::WordCount);

  // Common right-side actions: tag + attachment + spacer + layout toggle + find-and-replace.
  addCommonToolBarActions(toolBar);

  // Print action.
  {
    auto *printAction = addAction(toolBar, ViewWindowToolBarHelper2::Print);
    connect(printAction, &QAction::triggered, this, [this]() {
      auto printer = PrintUtils::promptForPrint(
          m_editor->getTextEdit()->hasSelection(), this);
      if (printer) {
        m_editor->getTextEdit()->print(printer.data());
      }
    });
  }
}

void TextViewWindow2::connectEditorSignals() {
  // Forward focus signal.
  connect(m_editor, &vte::VTextEditor::focusIn, this,
          [this]() { emit focused(this); });

  // Content change: use the new auto-save dirty flag approach.
  // Instead of writing content to buffer on every keystroke,
  // just mark dirty and let BufferService timer handle the sync.
  connect(m_editor->getTextEdit(), &vte::VTextEdit::contentsChanged, this, [this]() {
    if (m_propagateEditorToBuffer) {
      onEditorContentsChanged();
    }
  });
}

void TextViewWindow2::syncEditorFromBuffer() {
  const bool old = m_propagateEditorToBuffer;
  m_propagateEditorToBuffer = false;

  auto state = TextViewWindowController::prepareBufferState(getBuffer());
  if (state.valid) {
    m_editor->setSyntax(state.syntaxSuffix);
    m_editor->setReadOnly(state.readOnly);
    m_editor->setText(state.content);
    m_editor->setModified(state.modified);
  } else {
    m_editor->setSyntax(QString());
    m_editor->setReadOnly(true);
    m_editor->setText(QString());
    m_editor->setModified(false);
  }

  m_lastKnownRevision = state.revision;
  m_propagateEditorToBuffer = old;
}

QString TextViewWindow2::getLatestContent() const { return m_editor->getText(); }

void TextViewWindow2::setModified(bool p_modified) { m_editor->setModified(p_modified); }

void TextViewWindow2::setMode(ViewWindowMode p_mode) {
  Q_UNUSED(p_mode);
  Q_ASSERT(false);
}

int TextViewWindow2::getCursorPosition() const {
  if (m_editor) {
    return m_editor->getTextEdit()->textCursor().blockNumber();
  }
  return -1;
}

int TextViewWindow2::getScrollPosition() const {
  if (m_editor) {
    return m_editor->getTextEdit()->verticalScrollBar()->value();
  }
  return -1;
}

void TextViewWindow2::handleEditorConfigChange() {
  // Always update layout mode (WidgetConfig changes don't affect editor config revision).
  ViewWindow2::handleEditorConfigChange();

  if (m_controller->checkAndUpdateConfigRevision()) {
    auto *configMgr = getServices().get<ConfigMgr2>();
    const auto &editorConfig = configMgr->getEditorConfig();
    const auto &textEditorConfig = editorConfig.getTextEditorConfig();

    auto *themeService = getServices().get<ThemeService>();
    auto themeFile = themeService->getFile(Theme::File::TextEditorStyle);
    auto syntaxTheme = themeService->getEditorHighlightTheme();
    qreal scaleFactor = WidgetUtils::calculateScaleFactor();

    auto config = TextViewWindowController::buildTextEditorConfig(
        editorConfig, textEditorConfig, themeFile, syntaxTheme, scaleFactor);
    m_editor->setConfig(config);

    updateEditorFromConfig();
  }
}

void TextViewWindow2::updateEditorFromConfig() {
  auto snapshot = m_controller->currentEditorConfig();

  if (snapshot.zoomDelta != 0) {
    m_editor->zoom(snapshot.zoomDelta);
  }

  {
    vte::Key leaderKey(snapshot.shortcutLeaderKey);
    m_editor->setLeaderKeyToSkip(leaderKey.m_key, leaderKey.m_modifiers);
  }
}

void TextViewWindow2::scrollUp() {
  QScrollBar *vbar = m_editor->getTextEdit()->verticalScrollBar();
  if (vbar && (vbar->minimum() != vbar->maximum())) {
    vbar->triggerAction(QAbstractSlider::SliderSingleStepAdd);
  }
}

void TextViewWindow2::scrollDown() {
  QScrollBar *vbar = m_editor->getTextEdit()->verticalScrollBar();
  if (vbar && (vbar->minimum() != vbar->maximum())) {
    vbar->triggerAction(QAbstractSlider::SliderSingleStepSub);
  }
}

void TextViewWindow2::zoom(bool p_zoomIn) {
  m_editor->zoom(m_editor->zoomDelta() + (p_zoomIn ? 1 : -1));
  int delta = m_controller->persistZoomDelta(m_editor->zoomDelta());
  showZoomDelta(delta);
}

// ============ Find and Replace ============

void TextViewWindow2::handleFindTextChanged(const QString &p_text, FindOptions p_options) {
  TextViewWindowHelper::handleFindTextChanged(this, p_text, p_options);
}

void TextViewWindow2::handleFindNext(const QStringList &p_texts, FindOptions p_options) {
  TextViewWindowHelper::handleFindNext(this, p_texts, p_options);
}

void TextViewWindow2::handleReplace(const QString &p_text, FindOptions p_options,
                                    const QString &p_replaceText) {
  TextViewWindowHelper::handleReplace(this, p_text, p_options, p_replaceText);
}

void TextViewWindow2::handleReplaceAll(const QString &p_text, FindOptions p_options,
                                       const QString &p_replaceText) {
  TextViewWindowHelper::handleReplaceAll(this, p_text, p_options, p_replaceText);
}

void TextViewWindow2::handleFindAndReplaceWidgetClosed() {
  TextViewWindowHelper::clearSearchHighlights(this);
}

QString TextViewWindow2::selectedText() const {
  if (m_editor) {
    return m_editor->getTextEdit()->selectedText();
  }
  return QString();
}

void TextViewWindow2::fetchWordCountInfo(
    const std::function<void(const WordCountInfo &)> &p_callback) const {
  auto text = selectedText();
  if (!text.isEmpty()) {
    auto info = WordCountPanel::calculateWordCount(text);
    info.m_isSelection = true;
    p_callback(info);
    return;
  }

  auto info = WordCountPanel::calculateWordCount(getLatestContent());
  p_callback(info);
}
