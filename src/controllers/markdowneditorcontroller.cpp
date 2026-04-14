#include "markdowneditorcontroller.h"

#include <QFileInfo>

#include <vtextedit/markdowneditorconfig.h>
#include <vtextedit/texteditorconfig.h>
#include <vtextedit/theme.h>
#include <vtextedit/vtextedit.h>

#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/editorconfig.h>
#include <core/markdowneditorconfig.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/texteditorconfig.h>

using namespace vnotex;

MarkdownEditorController::MarkdownEditorController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {}

bool MarkdownEditorController::checkAndUpdateConfigRevision() {
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

  if (m_markdownEditorConfigRevision != editorConfig.getMarkdownEditorConfig().revision()) {
    changed = true;
    m_markdownEditorConfigRevision = editorConfig.getMarkdownEditorConfig().revision();
  }

  return changed;
}

QSharedPointer<vte::MarkdownEditorConfig> MarkdownEditorController::buildMarkdownEditorConfig(
    const EditorConfig &p_editorConfig, const MarkdownEditorConfig &p_mdConfig,
    const QString &p_themeFile, const QString &p_syntaxTheme, qreal p_scaleFactor,
    int p_maxContentWidth) {
  // Build base text editor config from the TextEditorConfig within MarkdownEditorConfig.
  const auto &textConfig = p_mdConfig.getTextEditorConfig();

  auto textEditorConfig = QSharedPointer<vte::TextEditorConfig>::create();

  textEditorConfig->m_viConfig = p_editorConfig.getViConfig();

  if (!p_themeFile.isEmpty()) {
    textEditorConfig->m_theme = vte::Theme::createThemeFromFile(p_themeFile);
  }

  textEditorConfig->m_syntaxTheme = p_syntaxTheme;

  switch (textConfig.getLineNumberType()) {
  case TextEditorConfig::LineNumberType::Absolute:
    textEditorConfig->m_lineNumberType = vte::VTextEditor::LineNumberType::Absolute;
    break;
  case TextEditorConfig::LineNumberType::Relative:
    textEditorConfig->m_lineNumberType = vte::VTextEditor::LineNumberType::Relative;
    break;
  case TextEditorConfig::LineNumberType::None:
    textEditorConfig->m_lineNumberType = vte::VTextEditor::LineNumberType::None;
    break;
  }

  textEditorConfig->m_textFoldingEnabled = textConfig.getTextFoldingEnabled();

  switch (textConfig.getInputMode()) {
  case TextEditorConfig::InputMode::ViMode:
    textEditorConfig->m_inputMode = vte::InputMode::ViMode;
    break;
  case TextEditorConfig::InputMode::VscodeMode:
    textEditorConfig->m_inputMode = vte::InputMode::VscodeMode;
    break;
  default:
    textEditorConfig->m_inputMode = vte::InputMode::NormalMode;
    break;
  }

  textEditorConfig->m_scaleFactor = p_scaleFactor;

  switch (textConfig.getCenterCursor()) {
  case TextEditorConfig::CenterCursor::NeverCenter:
    textEditorConfig->m_centerCursor = vte::CenterCursor::NeverCenter;
    break;
  case TextEditorConfig::CenterCursor::AlwaysCenter:
    textEditorConfig->m_centerCursor = vte::CenterCursor::AlwaysCenter;
    break;
  case TextEditorConfig::CenterCursor::CenterOnBottom:
    textEditorConfig->m_centerCursor = vte::CenterCursor::CenterOnBottom;
    break;
  }

  switch (textConfig.getWrapMode()) {
  case TextEditorConfig::WrapMode::NoWrap:
    textEditorConfig->m_wrapMode = vte::WrapMode::NoWrap;
    break;
  case TextEditorConfig::WrapMode::WordWrap:
    textEditorConfig->m_wrapMode = vte::WrapMode::WordWrap;
    break;
  case TextEditorConfig::WrapMode::WrapAnywhere:
    textEditorConfig->m_wrapMode = vte::WrapMode::WrapAnywhere;
    break;
  case TextEditorConfig::WrapMode::WordWrapOrAnywhere:
    textEditorConfig->m_wrapMode = vte::WrapMode::WordWrapOrAnywhere;
    break;
  }

  textEditorConfig->m_expandTab = textConfig.getExpandTabEnabled();
  textEditorConfig->m_tabStopWidth = textConfig.getTabStopWidth();
  textEditorConfig->m_highlightWhitespace = textConfig.getHighlightWhitespaceEnabled();
  textEditorConfig->m_lineSpacing = textConfig.getLineSpacing();
  textEditorConfig->m_maxContentWidth = p_maxContentWidth;

  switch (p_editorConfig.getLineEndingPolicy()) {
  case LineEndingPolicy::Platform:
    textEditorConfig->m_lineEndingPolicy = vte::LineEndingPolicy::Platform;
    break;
  case LineEndingPolicy::File:
    textEditorConfig->m_lineEndingPolicy = vte::LineEndingPolicy::File;
    break;
  case LineEndingPolicy::LF:
    textEditorConfig->m_lineEndingPolicy = vte::LineEndingPolicy::LF;
    break;
  case LineEndingPolicy::CRLF:
    textEditorConfig->m_lineEndingPolicy = vte::LineEndingPolicy::CRLF;
    break;
  case LineEndingPolicy::CR:
    textEditorConfig->m_lineEndingPolicy = vte::LineEndingPolicy::CR;
    break;
  }

  // Wrap the text editor config in a markdown editor config.
  auto editorConfig = QSharedPointer<vte::MarkdownEditorConfig>::create(textEditorConfig);
  editorConfig->overrideTextFontFamily(p_mdConfig.getEditorOverriddenFontFamily());

  editorConfig->m_constrainInplacePreviewWidthEnabled =
      p_mdConfig.getConstrainInplacePreviewWidthEnabled();

  // Map InplacePreviewSources flags from vnotex -> vte.
  {
    auto srcs = p_mdConfig.getInplacePreviewSources();
    vte::MarkdownEditorConfig::InplacePreviewSources editorSrcs =
        vte::MarkdownEditorConfig::NoInplacePreview;
    if (srcs & MarkdownEditorConfig::InplacePreviewSource::ImageLink) {
      editorSrcs |= vte::MarkdownEditorConfig::ImageLink;
    }
    if (srcs & MarkdownEditorConfig::InplacePreviewSource::CodeBlock) {
      editorSrcs |= vte::MarkdownEditorConfig::CodeBlock;
    }
    if (srcs & MarkdownEditorConfig::InplacePreviewSource::Math) {
      editorSrcs |= vte::MarkdownEditorConfig::Math;
    }
    editorConfig->m_inplacePreviewSources = editorSrcs;
  }

  return editorConfig;
}

QSharedPointer<vte::TextEditorParameters>
MarkdownEditorController::buildMarkdownEditorParameters(const EditorConfig &p_editorConfig,
                                                        const MarkdownEditorConfig &p_mdConfig) {
  auto paras = QSharedPointer<vte::TextEditorParameters>::create();
  paras->m_spellCheckEnabled = p_mdConfig.isSpellCheckEnabled();
  paras->m_autoDetectLanguageEnabled = p_editorConfig.isSpellCheckAutoDetectLanguageEnabled();
  paras->m_defaultSpellCheckLanguage = p_editorConfig.getSpellCheckDefaultDictionary();
  return paras;
}

MarkdownEditorController::EditorConfigSnapshot
MarkdownEditorController::currentEditorConfig() const {
  EditorConfigSnapshot snapshot;

  auto *configMgr = m_services.get<ConfigMgr2>();
  const auto &coreConfig = configMgr->getCoreConfig();
  const auto &editorConfig = configMgr->getEditorConfig();
  const auto &mdConfig = editorConfig.getMarkdownEditorConfig();

  snapshot.zoomDelta = mdConfig.getTextEditorConfig().getZoomDelta();
  snapshot.shortcutLeaderKey = coreConfig.getShortcutLeaderKey();
  snapshot.sectionNumberEnabled =
      mdConfig.getSectionNumberMode() != MarkdownEditorConfig::SectionNumberMode::None;

  return snapshot;
}

MarkdownEditorController::BufferState
MarkdownEditorController::prepareBufferState(const Buffer2 &p_buffer) {
  BufferState state;

  if (p_buffer.isValid()) {
    state.content = QString::fromUtf8(p_buffer.peekContentRaw());
    auto resolved = p_buffer.resolvedPath();
    state.basePath = resolved.isEmpty() ? QString() : QFileInfo(resolved).path();
    state.readOnly = false;
    state.modified = p_buffer.isModified();
    state.valid = true;
    state.revision = p_buffer.getRevision();
  } else {
    state.valid = false;
    state.revision = 0;
  }

  return state;
}

bool MarkdownEditorController::shouldEnableSectionNumber(
    MarkdownEditorConfig::SectionNumberMode p_sectionNumberMode, int p_mode) {
  switch (p_sectionNumberMode) {
  case MarkdownEditorConfig::SectionNumberMode::None:
    return false;
  case MarkdownEditorConfig::SectionNumberMode::Read:
    return p_mode == static_cast<int>(ViewWindowMode::Read);
  case MarkdownEditorConfig::SectionNumberMode::Edit:
    return p_mode == static_cast<int>(ViewWindowMode::Edit);
  }
  return false;
}

int MarkdownEditorController::persistZoomDelta(int p_delta) {
  auto *configMgr = m_services.get<ConfigMgr2>();
  auto &mdConfig = configMgr->getEditorConfig().getMarkdownEditorConfig();
  mdConfig.getTextEditorConfig().setZoomDelta(p_delta);
  return p_delta;
}

qreal MarkdownEditorController::persistViewerZoomFactor(qreal p_factor) {
  auto *configMgr = m_services.get<ConfigMgr2>();
  auto &mdConfig = configMgr->getEditorConfig().getMarkdownEditorConfig();
  mdConfig.setZoomFactorInReadMode(p_factor);
  return p_factor;
}

MarkdownEditorController::PreviewHelperConfig
MarkdownEditorController::getPreviewHelperConfig(const MarkdownEditorConfig &p_mdConfig) {
  PreviewHelperConfig config;
  config.webPlantUmlEnabled = p_mdConfig.getWebPlantUml();
  config.webGraphvizEnabled = p_mdConfig.getWebGraphviz();

  const auto srcs = p_mdConfig.getInplacePreviewSources();
  config.inplacePreviewCodeBlocksEnabled = srcs & MarkdownEditorConfig::CodeBlock;
  config.inplacePreviewMathBlocksEnabled = srcs & MarkdownEditorConfig::Math;

  return config;
}
