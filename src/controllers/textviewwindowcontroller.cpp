#include "textviewwindowcontroller.h"

#include <QFileInfo>

#include <vtextedit/texteditorconfig.h>
#include <vtextedit/theme.h>
#include <vtextedit/vtextedit.h>

#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/editorconfig.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/texteditorconfig.h>

using namespace vnotex;

TextViewWindowController::TextViewWindowController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {}

bool TextViewWindowController::checkAndUpdateConfigRevision() {
  bool changed = false;

  auto *configMgr = m_services.get<ConfigMgr2>();
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

QSharedPointer<vte::TextEditorConfig> TextViewWindowController::buildTextEditorConfig(
    const EditorConfig &p_editorConfig, const TextEditorConfig &p_config,
    const QString &p_themeFile, const QString &p_syntaxTheme, qreal p_scaleFactor,
    int p_maxContentWidth) {
  auto editorConfig = QSharedPointer<vte::TextEditorConfig>::create();

  editorConfig->m_viConfig = p_editorConfig.getViConfig();

  if (!p_themeFile.isEmpty()) {
    editorConfig->m_theme = vte::Theme::createThemeFromFile(p_themeFile);
  }

  editorConfig->m_syntaxTheme = p_syntaxTheme;

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

  editorConfig->m_scaleFactor = p_scaleFactor;

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
  editorConfig->m_lineSpacing = p_config.getLineSpacing();
  editorConfig->m_maxContentWidth = p_maxContentWidth;

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
TextViewWindowController::buildTextEditorParameters(const EditorConfig &p_editorConfig,
                                                    const TextEditorConfig &p_config) {
  auto paras = QSharedPointer<vte::TextEditorParameters>::create();
  paras->m_spellCheckEnabled = p_config.isSpellCheckEnabled();
  paras->m_autoDetectLanguageEnabled = p_editorConfig.isSpellCheckAutoDetectLanguageEnabled();
  paras->m_defaultSpellCheckLanguage = p_editorConfig.getSpellCheckDefaultDictionary();
  return paras;
}

TextViewWindowController::EditorConfigSnapshot
TextViewWindowController::currentEditorConfig() const {
  EditorConfigSnapshot snapshot;

  auto *configMgr = m_services.get<ConfigMgr2>();
  const auto &coreConfig = configMgr->getCoreConfig();
  const auto &editorConfig = configMgr->getEditorConfig();
  const auto &textEditorConfig = editorConfig.getTextEditorConfig();

  snapshot.zoomDelta = textEditorConfig.getZoomDelta();
  snapshot.shortcutLeaderKey = coreConfig.getShortcutLeaderKey();

  return snapshot;
}

TextViewWindowController::BufferState
TextViewWindowController::prepareBufferState(const Buffer2 &p_buffer) {
  BufferState state;

  if (p_buffer.isValid()) {
    const auto &nodeId = p_buffer.nodeId();
    state.syntaxSuffix = QFileInfo(nodeId.relativePath).suffix();
    state.readOnly = false;
    state.content = QString::fromUtf8(p_buffer.peekContentRaw());
    state.modified = p_buffer.isModified();
    state.valid = true;
    state.revision = p_buffer.getRevision();
  } else {
    state.valid = false;
    state.revision = 0;
  }

  return state;
}

int TextViewWindowController::persistZoomDelta(int p_delta) {
  auto *configMgr = m_services.get<ConfigMgr2>();
  auto &textEditorConfig = configMgr->getEditorConfig().getTextEditorConfig();
  textEditorConfig.setZoomDelta(p_delta);
  return p_delta;
}
