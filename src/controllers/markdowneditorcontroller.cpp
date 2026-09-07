#include "markdowneditorcontroller.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>

#include <vtextedit/markdowneditorconfig.h>
#include <vtextedit/markdownutils.h>
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
#include <utils/pathutils.h>

using namespace vnotex;

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

  p_editorConfig->m_autoFormatTableSourceEnabled = p_mdConfig.getAlignTableSourceEnabled();

  p_editorConfig->m_autoFoldPreviewedBlocksEnabled = p_mdConfig.getAutoFoldPreviewedBlocksEnabled();

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

struct HeadingMarkerReplacement {
  int m_start = -1;
  int m_end = -1;
  QString m_text;
};

bool makeHeadingMarkerReplacement(const QString &p_text,
                                  const MarkdownEditorController::HeadingBlockInfo &p_heading,
                                  int p_newLevel, HeadingMarkerReplacement &p_replacement) {
  const QString source = p_text.mid(p_heading.startPos, p_heading.endPos - p_heading.startPos);
  static const QRegularExpression atxPattern(QStringLiteral("\\A( {0,3})(#{1,6})(?=[\\t ]|$)"));
  const auto atxMatch = atxPattern.match(source);
  if (atxMatch.hasMatch()) {
    if (atxMatch.capturedLength(2) != p_heading.level) {
      return false;
    }
    p_replacement.m_start = p_heading.startPos + atxMatch.capturedStart(2);
    p_replacement.m_end = p_replacement.m_start + atxMatch.capturedLength(2);
    p_replacement.m_text = QString(p_newLevel, QLatin1Char('#'));
    return true;
  }

  const int underlineStart = source.lastIndexOf(QLatin1Char('\n'));
  if (underlineStart <= 0) {
    return false;
  }

  const QString underline = source.mid(underlineStart + 1);
  static const QRegularExpression setextPattern(QStringLiteral("\\A( {0,3})(=+|-+)([\\t ]*)\\z"));
  const auto setextMatch = setextPattern.match(underline);
  if (!setextMatch.hasMatch()) {
    return false;
  }

  const int parsedLevel = setextMatch.captured(2).startsWith(QLatin1Char('=')) ? 1 : 2;
  if (parsedLevel != p_heading.level) {
    return false;
  }

  if (p_newLevel <= 2) {
    p_replacement.m_start = p_heading.startPos + underlineStart + 1 + setextMatch.capturedStart(2);
    p_replacement.m_end = p_replacement.m_start + setextMatch.capturedLength(2);
    p_replacement.m_text = QString(setextMatch.capturedLength(2),
                                   p_newLevel == 1 ? QLatin1Char('=') : QLatin1Char('-'));
    return true;
  }

  const QString titleLine = source.left(underlineStart);
  int indentation = 0;
  while (indentation < titleLine.size() && indentation < 3 &&
         titleLine[indentation] == QLatin1Char(' ')) {
    ++indentation;
  }
  if (titleLine.mid(indentation).isEmpty()) {
    return false;
  }

  p_replacement.m_start = p_heading.startPos;
  p_replacement.m_end = p_heading.endPos;
  p_replacement.m_text = titleLine.left(indentation) + QString(p_newLevel, QLatin1Char('#')) +
                         QLatin1Char(' ') + titleLine.mid(indentation);
  return true;
}

int mapThroughHeadingReplacements(int p_position,
                                  const QVector<HeadingMarkerReplacement> &p_replacements) {
  int delta = 0;
  for (const auto &replacement : p_replacements) {
    if (p_position < replacement.m_start) {
      break;
    }

    const int replacementLength = replacement.m_text.size();
    if (p_position <= replacement.m_end) {
      return replacement.m_start + delta +
             qMin(p_position - replacement.m_start, replacementLength);
    }
    delta += replacementLength - (replacement.m_end - replacement.m_start);
  }
  return p_position + delta;
}
} // namespace

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
  const auto &editorConfig = configMgr->getEditorConfig();
  const auto &mdConfig = editorConfig.getMarkdownEditorConfig();

  snapshot.zoomDelta = mdConfig.getTextEditorConfig().getZoomDelta();
  snapshot.shortcutLeaderKey = coreConfig.getShortcutLeaderKey();

  return snapshot;
}

MarkdownEditorController::BufferState
MarkdownEditorController::prepareBufferState(const Buffer2 &p_buffer) {
  BufferState state;

  if (p_buffer.isValid()) {
    state.content = p_buffer.decode(p_buffer.peekContentRaw());
    auto resolved = p_buffer.resolvedPath();
    state.basePath = resolved.isEmpty() ? QString() : QFileInfo(resolved).path();
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

MarkdownEditorController::HeadingBlockMoveResult MarkdownEditorController::reorderHeadingBlock(
    QTextDocument *p_document, const QVector<HeadingBlockInfo> &p_headings,
    int p_sourceHeadingIndex, int p_beforeHeadingIndex, int p_targetLevel, int p_cursorPosition,
    int p_cursorAnchor, int p_selectionStart, int p_selectionEnd) {
  HeadingBlockMoveResult result;
  result.cursorPosition = p_cursorPosition;
  result.cursorAnchor = p_cursorAnchor;
  result.selectionStart = p_selectionStart;
  result.selectionEnd = p_selectionEnd;

  if (!p_document || p_sourceHeadingIndex < 0 || p_sourceHeadingIndex >= p_headings.size() ||
      p_targetLevel < 1 || p_targetLevel > 6) {
    return result;
  }

  const QString text = p_document->toPlainText();
  int previousEnd = -1;
  for (const auto &heading : p_headings) {
    if (heading.startPos < 0 || heading.endPos < 0) {
      if (heading.startPos != -1 || heading.endPos != -1) {
        return result;
      }
      continue;
    }
    if (heading.level < 1 || heading.level > 6 || heading.startPos < previousEnd ||
        heading.endPos <= heading.startPos || heading.endPos > text.size()) {
      return result;
    }
    HeadingMarkerReplacement validation;
    if (!makeHeadingMarkerReplacement(text, heading, heading.level, validation)) {
      return result;
    }
    previousEnd = heading.endPos;
  }

  const auto &source = p_headings[p_sourceHeadingIndex];
  if (source.startPos < 0 || source.endPos < 0) {
    return result;
  }

  int sourceEndHeadingIndex = p_sourceHeadingIndex + 1;
  while (sourceEndHeadingIndex < p_headings.size()) {
    const auto &heading = p_headings[sourceEndHeadingIndex];
    if (heading.startPos >= 0 && heading.level <= source.level) {
      break;
    }
    ++sourceEndHeadingIndex;
  }

  if (p_beforeHeadingIndex >= 0) {
    if (p_beforeHeadingIndex >= p_headings.size() ||
        p_headings[p_beforeHeadingIndex].startPos < 0 ||
        (p_beforeHeadingIndex >= p_sourceHeadingIndex &&
         p_beforeHeadingIndex < sourceEndHeadingIndex)) {
      return result;
    }
  }

  const int levelDelta = p_targetLevel - source.level;
  QVector<HeadingMarkerReplacement> replacements;
  for (int i = p_sourceHeadingIndex; i < sourceEndHeadingIndex; ++i) {
    const auto &heading = p_headings[i];
    if (heading.startPos < 0) {
      continue;
    }
    const int newLevel = heading.level + levelDelta;
    if (newLevel < 1 || newLevel > 6) {
      return result;
    }
    HeadingMarkerReplacement replacement;
    if (!makeHeadingMarkerReplacement(text, heading, newLevel, replacement)) {
      return result;
    }
    replacements.append(replacement);
  }

  QVector<int> originalOrder;
  QVector<int> movedOrder;
  QVector<int> remainingOrder;
  for (int i = 0; i < p_headings.size(); ++i) {
    if (p_headings[i].startPos < 0) {
      continue;
    }
    originalOrder.append(i);
    if (i >= p_sourceHeadingIndex && i < sourceEndHeadingIndex) {
      movedOrder.append(i);
    } else {
      remainingOrder.append(i);
    }
  }

  int orderInsertion = remainingOrder.size();
  if (p_beforeHeadingIndex >= 0) {
    orderInsertion = remainingOrder.indexOf(p_beforeHeadingIndex);
    if (orderInsertion < 0) {
      return result;
    }
  }
  QVector<int> reordered = remainingOrder;
  for (int i = 0; i < movedOrder.size(); ++i) {
    reordered.insert(orderInsertion + i, movedOrder[i]);
  }
  if (reordered == originalOrder && levelDelta == 0) {
    return result;
  }

  int sourceStart = source.startPos;
  const int sourceEnd = sourceEndHeadingIndex < p_headings.size()
                            ? p_headings[sourceEndHeadingIndex].startPos
                            : text.size();
  const int destination =
      p_beforeHeadingIndex >= 0 ? p_headings[p_beforeHeadingIndex].startPos : text.size();
  bool carriedLeadingSeparator = false;
  if (sourceEnd == text.size() && !text.endsWith(QLatin1Char('\n')) && sourceStart > 0 &&
      text[sourceStart - 1] == QLatin1Char('\n')) {
    --sourceStart;
    carriedLeadingSeparator = true;
  }
  if (sourceEnd <= sourceStart || (destination > sourceStart && destination < sourceEnd)) {
    return result;
  }

  for (auto &replacement : replacements) {
    replacement.m_start -= sourceStart;
    replacement.m_end -= sourceStart;
  }

  QString movedText = text.mid(sourceStart, sourceEnd - sourceStart);
  for (int i = replacements.size() - 1; i >= 0; --i) {
    const auto &replacement = replacements[i];
    movedText.replace(replacement.m_start, replacement.m_end - replacement.m_start,
                      replacement.m_text);
  }

  bool rotatedLeadingSeparator = false;
  bool rotatedTrailingSeparator = false;
  if (carriedLeadingSeparator && destination < sourceStart &&
      movedText.startsWith(QLatin1Char('\n'))) {
    movedText.remove(0, 1);
    movedText.append(QLatin1Char('\n'));
    rotatedLeadingSeparator = true;
  } else if (destination == text.size() && !text.endsWith(QLatin1Char('\n')) &&
             sourceEnd < text.size() && movedText.endsWith(QLatin1Char('\n'))) {
    movedText.chop(1);
    movedText.prepend(QLatin1Char('\n'));
    rotatedTrailingSeparator = true;
  }

  QString remainingText = text;
  remainingText.remove(sourceStart, sourceEnd - sourceStart);
  const int insertionPosition =
      destination >= sourceEnd ? destination - (sourceEnd - sourceStart) : destination;
  const QString prefix = remainingText.left(insertionPosition);
  const QString suffix = remainingText.mid(insertionPosition);
  int addedLeadingSeparator = 0;
  if (!prefix.isEmpty() && !movedText.isEmpty() && !prefix.endsWith(QLatin1Char('\n')) &&
      !movedText.startsWith(QLatin1Char('\n'))) {
    movedText.prepend(QLatin1Char('\n'));
    addedLeadingSeparator = 1;
  }
  if (!suffix.isEmpty() && !movedText.isEmpty() && !movedText.endsWith(QLatin1Char('\n')) &&
      !suffix.startsWith(QLatin1Char('\n'))) {
    movedText.append(QLatin1Char('\n'));
  }

  auto mapPosition = [&](int p_position) {
    if (p_position < 0) {
      return -1;
    }
    const int boundedPosition = qBound(0, p_position, text.size());
    if (boundedPosition >= sourceStart && boundedPosition < sourceEnd) {
      int localPosition =
          mapThroughHeadingReplacements(boundedPosition - sourceStart, replacements);
      if (rotatedLeadingSeparator) {
        localPosition = qMax(0, localPosition - 1);
      } else if (rotatedTrailingSeparator) {
        ++localPosition;
      }
      localPosition += addedLeadingSeparator;
      return insertionPosition + localPosition;
    }

    int remainingPosition = boundedPosition;
    if (boundedPosition >= sourceEnd) {
      remainingPosition -= sourceEnd - sourceStart;
    }
    if (remainingPosition >= insertionPosition) {
      remainingPosition += movedText.size();
    }
    return remainingPosition;
  };

  QTextCursor editCursor(p_document);
  editCursor.beginEditBlock();
  editCursor.setPosition(sourceStart);
  editCursor.setPosition(sourceEnd, QTextCursor::KeepAnchor);
  editCursor.removeSelectedText();
  editCursor.setPosition(insertionPosition);
  editCursor.insertText(movedText);
  editCursor.endEditBlock();

  result.moved = true;
  result.cursorPosition = mapPosition(p_cursorPosition);
  result.cursorAnchor = mapPosition(p_cursorAnchor);
  result.selectionStart = mapPosition(p_selectionStart);
  result.selectionEnd = mapPosition(p_selectionEnd);
  return result;
}
