#include "markdowneditorcontroller.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>

#include <vtextedit/markdowneditorconfig.h>
#include <vtextedit/markdownhighlighterdata.h>
#include <vtextedit/markdownutils.h>
#include <vtextedit/texteditorconfig.h>
#include <vtextedit/theme.h>
#include <vtextedit/vtextedit.h>

#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/editorconfig.h>
#include <core/hooknames.h>
#include <core/markdowneditorconfig.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/hookmanager.h>
#include <core/sessionconfig.h>
#include <core/texteditorconfig.h>
#include <gui/utils/imageutils.h>
#include <utils/pathutils.h>
#include <utils/sectionnumberutils.h>

using namespace vnotex;

QVector<QString>
MarkdownEditorController::generateSectionNumbers(const QVector<vte::md::HeadingInfo> &p_headings,
                                                 const QString &p_pattern,
                                                 bool p_detectHeading1ForSectionNumber) {
  QVector<QString> result(p_headings.size());
  const auto levelFor = [](const vte::md::HeadingInfo &p_heading) {
    return p_heading.m_isEmpty || p_heading.m_isSetext ? 0 : p_heading.m_level;
  };
  const auto analysis =
      SectionNumberUtils::analyzeStructure(p_headings, levelFor, p_detectHeading1ForSectionNumber);
  if (analysis.m_skip) {
    return result;
  }
  const auto pattern = SectionNumberUtils::normalizePattern(p_pattern);
  int maximumLevel = analysis.m_baseLevel;
  for (int i = analysis.m_firstNumberedHeading; i < p_headings.size(); ++i) {
    maximumLevel = qMax(maximumLevel, levelFor(p_headings[i]));
  }
  QVector<int> numbers(maximumLevel + 1, 0);
  for (int i = analysis.m_firstNumberedHeading; i < p_headings.size(); ++i) {
    const int level = levelFor(p_headings[i]);
    if (level <= 0) {
      continue;
    }
    SectionNumberUtils::increaseSectionNumber(numbers, level, analysis.m_baseLevel);
    result[i] = SectionNumberUtils::joinSectionNumber(numbers, pattern);
  }
  return result;
}

namespace {
// The Markdown-specific vnotex -> vte field mapping shared by both builders. The two builders
// differ only in how the theme is constructed; everything from here on is common, so a new
// field must be added HERE and nowhere else. Duplicating it once made it possible to wire a
// field into the file-based builder only, which is silently inert: every production caller
// (markdownviewwindow2.cpp) goes through buildMarkdownEditorConfigFromContent.
void applyMarkdownConfigFields(const MarkdownEditorConfig &p_mdConfig,
                               const QSharedPointer<vte::MarkdownEditorConfig> &p_editorConfig) {
  p_editorConfig->m_constrainInplacePreviewWidthEnabled =
      p_mdConfig.getConstrainInplacePreviewWidthEnabled();

  p_editorConfig->m_autoNumberOrderedListsEnabled = p_mdConfig.getAutoNumberOrderedListsEnabled();
  p_editorConfig->m_autoFormatTableSourceEnabled = p_mdConfig.getAlignTableSourceEnabled();

  p_editorConfig->m_autoFoldPreviewedBlocksEnabled = p_mdConfig.getAutoFoldPreviewedBlocksEnabled();

  switch (p_mdConfig.getConcealmentMode()) {
  case MarkdownEditorConfig::ConcealmentMode::None:
    p_editorConfig->m_concealRules.clear();
    break;
  case MarkdownEditorConfig::ConcealmentMode::Passive:
    p_editorConfig->m_concealRules.setPreset(vte::MarkdownConcealPreset::Passive);
    break;
  case MarkdownEditorConfig::ConcealmentMode::Moderate:
    p_editorConfig->m_concealRules.setPreset(vte::MarkdownConcealPreset::Moderate);
    break;
  case MarkdownEditorConfig::ConcealmentMode::Aggressive:
    p_editorConfig->m_concealRules.setPreset(vte::MarkdownConcealPreset::Aggressive);
    break;
  }

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
    if (srcs & MarkdownEditorConfig::InplacePreviewSource::Table) {
      editorSrcs |= vte::MarkdownEditorConfig::Table;
    }
    p_editorConfig->m_inplacePreviewSources = editorSrcs;
  }
}

} // namespace

MarkdownEditorController::MarkdownEditorController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {}

bool MarkdownEditorController::beginConcealmentConfirmation() {
  auto &session = m_services.get<ConfigMgr2>()->getSessionConfig();
  if (session.getHasConfirmedMarkdownEditorConcealment()) {
    return false;
  }
  session.setHasConfirmedMarkdownEditorConcealment(true);
  return true;
}

void MarkdownEditorController::setConcealmentMode(MarkdownEditorConfig::ConcealmentMode p_mode) {
  auto &config = m_services.get<ConfigMgr2>()->getEditorConfig().getMarkdownEditorConfig();
  const auto previous = config.getConcealmentMode();
  config.setConcealmentMode(p_mode);
  if (config.getConcealmentMode() == previous) {
    return;
  }
  m_services.get<HookManager>()->doAction(HookNames::ConfigEditorChanged);
}

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

  const auto &sessionConfig = configMgr->getSessionConfig();
  if (m_textEditorZoomDelta != sessionConfig.getTextEditorZoomDelta()) {
    changed = true;
    m_textEditorZoomDelta = sessionConfig.getTextEditorZoomDelta();
  }
  if (m_markdownZoomFactorInReadMode != sessionConfig.getMarkdownZoomFactorInReadMode()) {
    changed = true;
    m_markdownZoomFactorInReadMode = sessionConfig.getMarkdownZoomFactorInReadMode();
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

  applyMarkdownConfigFields(p_mdConfig, editorConfig);

  return editorConfig;
}

QSharedPointer<vte::MarkdownEditorConfig>
MarkdownEditorController::buildMarkdownEditorConfigFromContent(
    const EditorConfig &p_editorConfig, const MarkdownEditorConfig &p_mdConfig,
    const QString &p_themeContent, const QString &p_syntaxTheme, qreal p_scaleFactor,
    int p_maxContentWidth) {
  // Build base text editor config from the TextEditorConfig within MarkdownEditorConfig.
  const auto &textConfig = p_mdConfig.getTextEditorConfig();

  auto textEditorConfig = QSharedPointer<vte::TextEditorConfig>::create();

  textEditorConfig->m_viConfig = p_editorConfig.getViConfig();

  if (!p_themeContent.isEmpty()) {
    textEditorConfig->m_theme = vte::Theme::createThemeFromContent(p_themeContent);
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

  // Pre-seed an empty placeholder theme so vte::MarkdownEditorConfig's constructor
  // short-circuits fillDefaultTheme(). The default-theme code path reads the
  // bundled :/vtextedit/editor/data/themes/default.theme resource via
  // Theme::loadStyleFormat() -> chooseAvailableFont() -> QFontDatabase(), and
  // QFontDatabase requires a live QGuiApplication. A controller must remain
  // headless-safe and must not depend on QFontDatabase.
  const bool themeWasEmpty = textEditorConfig->m_theme.isNull();
  if (themeWasEmpty) {
    textEditorConfig->m_theme = QSharedPointer<vte::Theme>::create();
  }

  // Wrap the text editor config in a markdown editor config.
  auto editorConfig = QSharedPointer<vte::MarkdownEditorConfig>::create(textEditorConfig);

  applyMarkdownConfigFields(p_mdConfig, editorConfig);

  if (themeWasEmpty) {
    // Honor the empty-content contract: caller passed no theme JSON, so leave
    // the theme slot null. Downstream consumers branch on this.
    editorConfig->m_textEditorConfig->m_theme.reset();
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

  snapshot.zoomDelta = configMgr->getSessionConfig().getTextEditorZoomDelta();
  snapshot.shortcutLeaderKey = coreConfig.getShortcutLeaderKey();

  return snapshot;
}

MarkdownEditorController::BufferState
MarkdownEditorController::prepareBufferState(const Buffer2 &p_buffer) {
  BufferState state;

  if (p_buffer.isValid()) {
    if (p_buffer.isEncrypted()) {
      VxCoreError error = VXCORE_OK;
      const auto content = p_buffer.peekContentRaw(&error);
      if (error != VXCORE_OK) {
        return state;
      }
      state.content = p_buffer.decode(content);
      // Protected previews have no filesystem base, even for relative links.
    } else {
      state.content = p_buffer.decode(p_buffer.peekContentRaw());
      auto resolved = p_buffer.resolvedPath();
      state.basePath = resolved.isEmpty() ? QString() : QFileInfo(resolved).path();
    }
    state.readOnly = p_buffer.isReadOnly();
    state.modified = p_buffer.isModified();
    state.valid = true;
    state.revision = p_buffer.getRevision();
  } else {
    state.valid = false;
    state.revision = 0;
  }

  return state;
}

bool MarkdownEditorController::insertImageAsBase64(QTextCursor &p_cursor, const QString &p_title,
                                                   const QString &p_altText,
                                                   const QByteArray &p_data) {
  if (p_cursor.isNull()) {
    return false;
  }
  QByteArray bytes;
  QByteArray mime;
  if (!ImageUtils::protectedImageData(p_data, bytes, mime)) {
    return false;
  }

  const auto content = p_cursor.document()->toPlainText();
  // Reserve every occurrence, not only parsed definitions: this also avoids
  // capturing an unresolved reference. Generated labels have no whitespace;
  // case folding is therefore the only Markdown label normalization needed.
  static const QRegularExpression labelToken(QStringLiteral("image-[0-9]+"),
                                             QRegularExpression::CaseInsensitiveOption);
  QSet<QString> labels;
  auto matches = labelToken.globalMatch(content);
  while (matches.hasNext()) {
    labels.insert(matches.next().captured().toCaseFolded());
  }
  int suffix = 1;
  QString label;
  do {
    label = QStringLiteral("image-%1").arg(suffix++);
  } while (labels.contains(label));

  QString description;
  description.reserve(p_title.size());
  const auto punctuation = QStringLiteral("\\[]*_`<>&!");
  for (const auto ch : p_title) {
    if (punctuation.contains(ch)) {
      description += QLatin1Char('\\');
    }
    description += ch == QLatin1Char('\n') || ch == QLatin1Char('\r') ? QLatin1Char(' ') : ch;
  }
  auto title = p_altText;
  title.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
  title.replace(QLatin1Char('"'), QStringLiteral("\\\""));
  title.replace(QLatin1Char('\n'), QLatin1Char(' '));
  const auto reference = QStringLiteral("![%1][%2]").arg(description, label);
  auto definition =
      QStringLiteral("[%1]: data:%2;base64,%3")
          .arg(label, QString::fromLatin1(mime), QString::fromLatin1(bytes.toBase64()));
  bytes.fill('\0');
  if (!title.isEmpty()) {
    definition += QStringLiteral(" \"%1\"").arg(title);
  }

  p_cursor.beginEditBlock();
  p_cursor.insertText(reference);
  const int position = p_cursor.position();
  QTextCursor end(p_cursor.document());
  end.movePosition(QTextCursor::End);
  // A blank line makes the definition independent of the preceding paragraph.
  end.insertText(QStringLiteral("\n\n") + definition + QLatin1Char('\n'));
  p_cursor.setPosition(position);
  p_cursor.endEditBlock();
  return true;
}

int MarkdownEditorController::persistZoomDelta(int p_delta) {
  auto *configMgr = m_services.get<ConfigMgr2>();
  configMgr->getSessionConfig().setTextEditorZoomDelta(p_delta);
  return p_delta;
}

qreal MarkdownEditorController::persistViewerZoomFactor(qreal p_factor) {
  auto *configMgr = m_services.get<ConfigMgr2>();
  configMgr->getSessionConfig().setMarkdownZoomFactorInReadMode(p_factor);
  return p_factor;
}

MarkdownEditorController::PreviewHelperConfig
MarkdownEditorController::getPreviewHelperConfig(const MarkdownEditorConfig &p_mdConfig) {
  PreviewHelperConfig config;
  config.webGraphvizEnabled = p_mdConfig.getWebGraphviz();

  const auto srcs = p_mdConfig.getInplacePreviewSources();
  config.inplacePreviewCodeBlocksEnabled = srcs & MarkdownEditorConfig::CodeBlock;
  config.inplacePreviewMathBlocksEnabled = srcs & MarkdownEditorConfig::Math;

  return config;
}

bool MarkdownEditorController::isLinkableHeadingLine(const QString &p_blockText) {
  const auto match = vte::MarkdownUtils::matchHeader(p_blockText);
  // m_header is already trimmed, so an empty one rejects lines like "## ",
  // whose anchor would be the empty string and whose link a bare '#'.
  return match.m_matched && !match.m_header.isEmpty();
}

QString MarkdownEditorController::composeHeadingLink(const QString &p_resolvedNotePath,
                                                     const QString &p_anchor) {
  QUrl url = PathUtils::pathToUrl(p_resolvedNotePath);
  // DecodedMode: the anchor arrives already decoded from the web side, so a
  // literal '%' must be encoded rather than parsed as the start of an escape.
  url.setFragment(p_anchor, QUrl::DecodedMode);
  // The returned string is NOT what finally lands on the clipboard verbatim:
  // ClipboardUtils::setLinkToClipboard feeds it back through
  // PathUtils::pathToUrl and re-serializes it (on Windows local files as
  // toString(QUrl::EncodeSpaces)). The percent-encoding of the clipboard text
  // is therefore decided there, not here; only the anchor content has to
  // survive that round trip (covered by test_markdown_heading_link's
  // anchorSurvivesClipboardReparse).
  return url.toString();
}
