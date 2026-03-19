#include "textviewwindow2.h"

#include <QAction>
#include <QFileInfo>
#include <QPrinter>
#include <QScrollBar>
#include <QToolBar>
#include <QToolButton>

#include <vtextedit/texteditorconfig.h>
#include <vtextedit/theme.h>
#include <vtextedit/vtextedit.h>

#include <core/configmgr2.h>
#include <core/coreconfig.h>
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
#include "wordcountpopup.h"
#include "wordcountpopup2.h"

#include <gui/utils/printutils.h>

using namespace vnotex;

TextViewWindow2::TextViewWindow2(ServiceLocator &p_services, const Buffer2 &p_buffer,
                                 QWidget *p_parent)
    : ViewWindow2(p_services, p_buffer, p_parent) {
  m_mode = ViewWindowMode::Edit;
  setupUI();
}

void TextViewWindow2::setupUI() {
  auto *configMgr = getServices().get<ConfigMgr2>();
  const auto &editorConfig = configMgr->getEditorConfig();
  const auto &textEditorConfig = editorConfig.getTextEditorConfig();

  updateConfigRevision();

  // Central widget: text editor.
  {
    m_editor =
        new TextEditor(createTextEditorConfig(editorConfig, textEditorConfig, getServices()),
                       createTextEditorParameters(editorConfig, textEditorConfig), this);
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

  // Save action.
  m_saveAction = ViewWindowToolBarHelper2::addAction(
      toolBar, ViewWindowToolBarHelper2::Save, getServices(), this);
  m_saveAction->setEnabled(false);
  connect(m_saveAction, &QAction::triggered, this, [this]() {
    save();
  });
  connect(this, &ViewWindow2::statusChanged, this, [this]() {
    m_saveAction->setEnabled(getBuffer().isValid() && isModified());
  });

  // Word count popup.
  {
    auto *wcAction = ViewWindowToolBarHelper2::addAction(
        toolBar, ViewWindowToolBarHelper2::WordCount, getServices(), this);
    auto *toolBtn = dynamic_cast<QToolButton *>(toolBar->widgetForAction(wcAction));
    Q_ASSERT(toolBtn);
    auto *popup = new WordCountPopup2(
        toolBtn,
        [this](WordCountPanel *p_panel) {
          auto text = m_editor->getTextEdit()->selectedText();
          bool isSelection = !text.isEmpty();
          if (!isSelection) {
            text = getLatestContent();
          }

          // Word count algorithm matching legacy TextViewWindowHelper::calculateWordCountInfo().
          int cns = 0;
          int wc = 0;
          int cc = text.size();
          // 0 - not in word; 1 - in English word; 2 - in non-English word.
          int state = 0;
          for (int i = 0; i < cc; ++i) {
            QChar ch = text[i];
            if (ch.isSpace()) {
              if (state) {
                state = 0;
              }
              continue;
            } else if (ch.unicode() < 128) {
              if (state != 1) {
                state = 1;
                ++wc;
              }
            } else {
              state = 2;
              ++wc;
            }
            ++cns;
          }

          p_panel->updateCount(isSelection, wc, cns, cc);
        },
        toolBar);
    toolBtn->setMenu(popup);
  }

  ViewWindowToolBarHelper2::addSpacer(toolBar);

  // Find and Replace action.
  {
    auto *findAction = ViewWindowToolBarHelper2::addAction(
        toolBar, ViewWindowToolBarHelper2::FindAndReplace, getServices(), this);
    connect(findAction, &QAction::triggered, this, [this]() {
      if (findAndReplaceWidgetVisible()) {
        hideFindAndReplaceWidget();
      } else {
        showFindAndReplaceWidget();
      }
    });
  }

  // Print action.
  {
    auto *printAction = ViewWindowToolBarHelper2::addAction(
        toolBar, ViewWindowToolBarHelper2::Print, getServices(), this);
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

  const auto &buffer = getBuffer();
  if (buffer.isValid()) {
    const auto &nodeId = buffer.nodeId();
    QString suffix = QFileInfo(nodeId.relativePath).suffix();
    m_editor->setSyntax(suffix);
    m_editor->setReadOnly(false);
    m_editor->setText(QString::fromUtf8(buffer.peekContentRaw()));
    m_editor->setModified(buffer.isModified());
  } else {
    m_editor->setSyntax(QString());
    m_editor->setReadOnly(true);
    m_editor->setText(QString());
    m_editor->setModified(false);
  }

  m_lastKnownRevision = buffer.isValid() ? buffer.getRevision() : 0;
  m_propagateEditorToBuffer = old;
}

QString TextViewWindow2::getLatestContent() const { return m_editor->getText(); }

void TextViewWindow2::setModified(bool p_modified) { m_editor->setModified(p_modified); }

void TextViewWindow2::setMode(ViewWindowMode p_mode) {
  Q_UNUSED(p_mode);
  Q_ASSERT(false);
}

void TextViewWindow2::handleEditorConfigChange() {
  if (updateConfigRevision()) {
    auto *configMgr = getServices().get<ConfigMgr2>();
    const auto &editorConfig = configMgr->getEditorConfig();
    const auto &textEditorConfig = editorConfig.getTextEditorConfig();

    auto config = createTextEditorConfig(editorConfig, textEditorConfig, getServices());
    m_editor->setConfig(config);

    updateEditorFromConfig();
  }
}

bool TextViewWindow2::updateConfigRevision() {
  bool changed = false;

  auto *configMgr = getServices().get<ConfigMgr2>();
  const auto &editorConfig = configMgr->getEditorConfig();

  if (m_editorConfigRevision != editorConfig.revision()) {
    changed = true;
    m_editorConfigRevision = editorConfig.revision();
  }

  if (m_textEditorConfigRevision != editorConfig.getTextEditorConfig().revision()) {
    changed = true;
    m_textEditorConfigRevision = editorConfig.getTextEditorConfig().revision();
  }

  return changed;
}

void TextViewWindow2::updateEditorFromConfig() {
  auto *configMgr = getServices().get<ConfigMgr2>();
  const auto &coreConfig = configMgr->getCoreConfig();
  const auto &editorConfig = configMgr->getEditorConfig();
  const auto &textEditorConfig = editorConfig.getTextEditorConfig();

  if (textEditorConfig.getZoomDelta() != 0) {
    m_editor->zoom(textEditorConfig.getZoomDelta());
  }

  {
    vte::Key leaderKey(coreConfig.getShortcutLeaderKey());
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

  auto *configMgr = getServices().get<ConfigMgr2>();
  auto &textEditorConfig = configMgr->getEditorConfig().getTextEditorConfig();
  textEditorConfig.setZoomDelta(m_editor->zoomDelta());

  showZoomDelta(m_editor->zoomDelta());
}

QSharedPointer<vte::TextEditorConfig>
TextViewWindow2::createTextEditorConfig(const EditorConfig &p_editorConfig,
                                        const TextEditorConfig &p_config,
                                        ServiceLocator &p_services) {
  auto *themeService = p_services.get<ThemeService>();
  auto editorConfig = QSharedPointer<vte::TextEditorConfig>::create();

  editorConfig->m_viConfig = p_editorConfig.getViConfig();

  {
    auto themeFile = themeService->getFile(Theme::File::TextEditorStyle);
    if (!themeFile.isEmpty()) {
      editorConfig->m_theme = vte::Theme::createThemeFromFile(themeFile);
    }
  }

  editorConfig->m_syntaxTheme = themeService->getEditorHighlightTheme();

  switch (p_config.getLineNumberType()) {
  case TextEditorConfig::LineNumberType::Absolute:
    editorConfig->m_lineNumberType = vte::VTextEditor::LineNumberType::Absolute;
    break;
  case TextEditorConfig::LineNumberType::Relative:
    editorConfig->m_lineNumberType = vte::VTextEditor::LineNumberType::Relative;
    break;
  case TextEditorConfig::LineNumberType::None:
    editorConfig->m_lineNumberType = vte::VTextEditor::LineNumberType::None;
    break;
  }

  editorConfig->m_textFoldingEnabled = p_config.getTextFoldingEnabled();

  switch (p_config.getInputMode()) {
  case TextEditorConfig::InputMode::ViMode:
    editorConfig->m_inputMode = vte::InputMode::ViMode;
    break;
  case TextEditorConfig::InputMode::VscodeMode:
    editorConfig->m_inputMode = vte::InputMode::VscodeMode;
    break;
  default:
    editorConfig->m_inputMode = vte::InputMode::NormalMode;
    break;
  }

  editorConfig->m_scaleFactor = WidgetUtils::calculateScaleFactor();

  switch (p_config.getCenterCursor()) {
  case TextEditorConfig::CenterCursor::NeverCenter:
    editorConfig->m_centerCursor = vte::CenterCursor::NeverCenter;
    break;
  case TextEditorConfig::CenterCursor::AlwaysCenter:
    editorConfig->m_centerCursor = vte::CenterCursor::AlwaysCenter;
    break;
  case TextEditorConfig::CenterCursor::CenterOnBottom:
    editorConfig->m_centerCursor = vte::CenterCursor::CenterOnBottom;
    break;
  }

  switch (p_config.getWrapMode()) {
  case TextEditorConfig::WrapMode::NoWrap:
    editorConfig->m_wrapMode = vte::WrapMode::NoWrap;
    break;
  case TextEditorConfig::WrapMode::WordWrap:
    editorConfig->m_wrapMode = vte::WrapMode::WordWrap;
    break;
  case TextEditorConfig::WrapMode::WrapAnywhere:
    editorConfig->m_wrapMode = vte::WrapMode::WrapAnywhere;
    break;
  case TextEditorConfig::WrapMode::WordWrapOrAnywhere:
    editorConfig->m_wrapMode = vte::WrapMode::WordWrapOrAnywhere;
    break;
  }

  editorConfig->m_expandTab = p_config.getExpandTabEnabled();
  editorConfig->m_tabStopWidth = p_config.getTabStopWidth();
  editorConfig->m_highlightWhitespace = p_config.getHighlightWhitespaceEnabled();

  switch (p_editorConfig.getLineEndingPolicy()) {
  case LineEndingPolicy::Platform:
    editorConfig->m_lineEndingPolicy = vte::LineEndingPolicy::Platform;
    break;
  case LineEndingPolicy::File:
    editorConfig->m_lineEndingPolicy = vte::LineEndingPolicy::File;
    break;
  case LineEndingPolicy::LF:
    editorConfig->m_lineEndingPolicy = vte::LineEndingPolicy::LF;
    break;
  case LineEndingPolicy::CRLF:
    editorConfig->m_lineEndingPolicy = vte::LineEndingPolicy::CRLF;
    break;
  case LineEndingPolicy::CR:
    editorConfig->m_lineEndingPolicy = vte::LineEndingPolicy::CR;
    break;
  }

  return editorConfig;
}

QSharedPointer<vte::TextEditorParameters>
TextViewWindow2::createTextEditorParameters(const EditorConfig &p_editorConfig,
                                            const TextEditorConfig &p_config) {
  auto paras = QSharedPointer<vte::TextEditorParameters>::create();
  paras->m_spellCheckEnabled = p_config.isSpellCheckEnabled();
  paras->m_autoDetectLanguageEnabled = p_editorConfig.isSpellCheckAutoDetectLanguageEnabled();
  paras->m_defaultSpellCheckLanguage = p_editorConfig.getSpellCheckDefaultDictionary();
  return paras;
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
