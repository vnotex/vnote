#ifndef TEXTVIEWWINDOWHELPER_H
#define TEXTVIEWWINDOWHELPER_H

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSharedPointer>
#include <QTextBlock>
#include <QTextCursor>

#include <core/configmgr.h>
#include <core/services/snippetcoreservice.h>
#include <core/texteditorconfig.h>
#include <gui/services/themeservice.h>
#include <gui/utils/widgetutils.h>
#include <search/searchtoken.h>
#include <snippet/snippetmgr.h>
#include <vtextedit/texteditorconfig.h>
#include <vtextedit/vtextedit.h>

#include "quickselector.h"
#include "viewwindow.h"
#include "viewwindow2.h"
#include "wordcountpanel.h"

namespace vte {
class ViConfig;
}

namespace vnotex {
class TextEditorConfig;

// Abstract some common logics for TextViewWindow and MarkdownViewWindow.
class TextViewWindowHelper {
public:
  TextViewWindowHelper() = delete;

  template <typename _ViewWindow> static void connectEditor(_ViewWindow *p_win) {
    auto editor = p_win->m_editor;
    p_win->connect(editor, &vte::VTextEditor::focusIn, p_win,
                   [p_win]() { emit p_win->focused(p_win); });

    p_win->connect(editor->getTextEdit(), &vte::VTextEdit::contentsChanged, p_win,
                   [p_win, editor]() {
                     if (p_win->m_propogateEditorToBuffer) {
                       p_win->getBuffer()->setModified(editor->isModified());
                       p_win->getBuffer()->invalidateContent(p_win, [p_win](int p_revision) {
                         p_win->setBufferRevisionAfterInvalidation(p_revision);
                       });
                     }
                   });
  }

  template <typename _ViewWindow> static void handleBufferChanged(_ViewWindow *p_win) {
    p_win->m_propogateEditorToBuffer = false;
    p_win->syncEditorFromBuffer();
    p_win->m_propogateEditorToBuffer = true;

    emit p_win->statusChanged();
    emit p_win->modeChanged();
  }

  static QSharedPointer<vte::TextEditorConfig>
  createTextEditorConfig(const TextEditorConfig &p_config,
                         const QSharedPointer<vte::ViConfig> &p_viConfig,
                         const QString &p_themeFile, const QString &p_syntaxTheme,
                         LineEndingPolicy p_lineEndingPolicy) {
    auto editorConfig = QSharedPointer<vte::TextEditorConfig>::create();

    editorConfig->m_viConfig = p_viConfig;

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

    editorConfig->m_lineSpacing = p_config.getLineSpacing();

    switch (p_lineEndingPolicy) {
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

  static vte::FindFlags toEditorFindFlags(FindOptions p_options) {
    vte::FindFlags flags;
    if (p_options & FindOption::FindBackward) {
      flags |= vte::FindFlag::FindBackward;
    }
    if (p_options & FindOption::CaseSensitive) {
      flags |= vte::FindFlag::CaseSensitive;
    }
    if (p_options & FindOption::WholeWordOnly) {
      flags |= vte::FindFlag::WholeWordOnly;
    }
    if (p_options & FindOption::RegularExpression) {
      flags |= vte::FindFlag::RegularExpression;
    }
    return flags;
  }

  template <typename _ViewWindow>
  static void handleFindTextChanged(_ViewWindow *p_win, const QString &p_text,
                                    FindOptions p_options) {
    if (p_options & FindOption::IncrementalSearch) {
      p_win->m_editor->peekText(p_text, toEditorFindFlags(p_options));
    }
  }

  template <typename _ViewWindow>
  static void handleFindNext(_ViewWindow *p_win, const QStringList &p_texts,
                             FindOptions p_options) {
    const auto result = p_win->m_editor->findText(p_texts, toEditorFindFlags(p_options));
    p_win->showFindResult(p_texts, result.m_totalMatches, result.m_currentMatchIndex);
  }

  template <typename _ViewWindow>
  static void handleReplace(_ViewWindow *p_win, const QString &p_text, FindOptions p_options,
                            const QString &p_replaceText) {
    const auto result =
        p_win->m_editor->replaceText(p_text, toEditorFindFlags(p_options), p_replaceText);
    p_win->showReplaceResult(p_text, result.m_totalMatches);
  }

  template <typename _ViewWindow>
  static void handleReplaceAll(_ViewWindow *p_win, const QString &p_text, FindOptions p_options,
                               const QString &p_replaceText) {
    const auto result =
        p_win->m_editor->replaceAll(p_text, toEditorFindFlags(p_options), p_replaceText);
    p_win->showReplaceResult(p_text, result.m_totalMatches);
  }

  template <typename _ViewWindow> static void clearSearchHighlights(_ViewWindow *p_win) {
    p_win->m_editor->clearIncrementalSearchHighlight();
    p_win->m_editor->clearSearchHighlight();
  }

  template <typename _ViewWindow>
  static void applySnippet(_ViewWindow *p_win, const QString &p_name) {
    if (p_win->m_editor->isReadOnly() || p_name.isEmpty()) {
      qWarning() << "failed to apply snippet" << p_name << "to a read-only buffer";
      return;
    }

    SnippetMgr::getInst().applySnippet(p_name, p_win->m_editor->getTextEdit(),
                                       SnippetMgr::generateOverrides(p_win->getBuffer()));
    p_win->m_editor->enterInsertModeIfApplicable();
    p_win->showMessage(vnotex::ViewWindow::tr("Snippet applied: %1").arg(p_name));
  }

  template <typename _ViewWindow> static void applySnippet(_ViewWindow *p_win) {
    if (p_win->m_editor->isReadOnly()) {
      qWarning() << "failed to apply snippet to a read-only buffer";
      return;
    }

    QString snippetName;

    auto textEdit = p_win->m_editor->getTextEdit();
    if (!textEdit->hasSelection()) {
      // Fetch the snippet symbol containing current cursor.
      auto cursor = textEdit->textCursor();
      const auto block = cursor.block();
      const auto text = block.text();
      const int pib = cursor.positionInBlock();
      QRegularExpression regExp(SnippetMgr::c_snippetSymbolRegExp);
      QRegularExpressionMatch match;
      int idx = text.lastIndexOf(regExp, pib, &match);
      if (idx >= 0 && (idx + match.capturedLength(0) >= pib)) {
        // Found one symbol under current cursor.
        snippetName = match.captured(1);
        if (!SnippetMgr::getInst().find(snippetName)) {
          p_win->showMessage(vnotex::ViewWindow::tr("Snippet (%1) not found").arg(snippetName));
          return;
        }

        // Remove the symbol and apply snippet later.
        cursor.setPosition(block.position() + idx);
        cursor.setPosition(block.position() + idx + match.capturedLength(0),
                           QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        textEdit->setTextCursor(cursor);
      }
    }

    if (snippetName.isEmpty()) {
      // Prompt for snippet.
      snippetName = promptForSnippet(p_win);
    }

    if (!snippetName.isEmpty()) {
      applySnippet(p_win, snippetName);
    }
  }

  template <typename _ViewWindow> static QString promptForSnippet(_ViewWindow *p_win) {
    const auto snippets = SnippetMgr::getInst().getSnippets();
    if (snippets.isEmpty()) {
      p_win->showMessage(vnotex::ViewWindow::tr("Snippet not available"));
      return QString();
    }

    QVector<QuickSelectorItem> items;
    for (const auto &snip : snippets) {
      items.push_back(QuickSelectorItem(snip->getName(), snip->getName(), snip->getDescription(),
                                        snip->getShortcutString()));
    }

    // Ownership will be transferred to showFloatingWidget().
    auto selector =
        new QuickSelector(nullptr, vnotex::ViewWindow::tr("Select Snippet"), items, true, p_win);
    auto ret = p_win->showFloatingWidget(selector);
    return ret.toString();
  }

  template <typename _ViewWindow> static QPoint getFloatingWidgetPosition(_ViewWindow *p_win) {
    auto textEdit = p_win->m_editor->getTextEdit();
    auto localPos = textEdit->cursorRect().bottomRight();
    if (!textEdit->rect().contains(localPos)) {
      localPos = QPoint(5, 5);
    }
    return textEdit->mapToGlobal(localPos);
  }

  template <typename _ViewWindow>
  static void findTextBySearchToken(_ViewWindow *p_win, const QSharedPointer<SearchToken> &p_token,
                                    int p_currentMatchLine) {
    const auto patterns = p_token->toPatterns();
    p_win->updateLastFindInfo(patterns.first, patterns.second);
    const auto result = p_win->m_editor->findText(
        patterns.first, toEditorFindFlags(patterns.second), 0, -1, p_currentMatchLine);
    p_win->showFindResult(patterns.first, result.m_totalMatches, result.m_currentMatchIndex);
  }

  static ViewWindow::WordCountInfo calculateWordCountInfo(const QString &p_text) {
    return WordCountPanel::calculateWordCount(p_text);
  }

  // ============ Snippet Support (New Architecture) ============

  // Result of snippet symbol detection under cursor.
  struct SnippetSymbolResult {
    bool found = false;
    QString name;
    int start = -1; // Position within the block
    int end = -1;   // Position within the block (exclusive)
  };

  // Generate snippet override keys from Buffer2's NodeIdentifier.
  // Returns QJsonObject with "note" (relative path) and "no" (basename without extension).
  template <typename _ViewWindow> static QJsonObject generateSnippetOverrides2(_ViewWindow *p_win) {
    QJsonObject overrides;
    const auto &nodeId = p_win->getBuffer().nodeId();
    overrides[QStringLiteral("note")] = nodeId.relativePath;
    QFileInfo fi(nodeId.relativePath);
    overrides[QStringLiteral("no")] = fi.completeBaseName();
    return overrides;
  }

  // Detect %name% snippet symbol under or adjacent to cursor.
  // Returns the symbol name and its block-relative positions.
  template <typename _ViewWindow>
  static SnippetSymbolResult detectSnippetSymbol2(_ViewWindow *p_win) {
    SnippetSymbolResult result;
    auto textEdit = p_win->m_editor->getTextEdit();
    auto cursor = textEdit->textCursor();
    const auto block = cursor.block();
    const auto text = block.text();
    const int pib = cursor.positionInBlock();

    static const QRegularExpression regExp(QStringLiteral("%([^%]+)%"));
    QRegularExpressionMatch match;
    int idx = text.lastIndexOf(regExp, pib, &match);
    if (idx >= 0 && (idx + match.capturedLength(0) >= pib)) {
      result.found = true;
      result.name = match.captured(1);
      result.start = idx;
      result.end = idx + match.capturedLength(0);
    }
    return result;
  }

  // Apply a snippet by name using SnippetCoreService.
  // Inserts the expanded text at the cursor, handles cursor offset and edit blocks.
  template <typename _ViewWindow>
  static void applySnippetByName2(_ViewWindow *p_win, const QString &p_name) {
    if (p_win->m_editor->isReadOnly() || p_name.isEmpty()) {
      qWarning() << "failed to apply snippet" << p_name << "to a read-only buffer";
      return;
    }

    auto *snippetSvc = p_win->getServices().template get<SnippetCoreService>();
    if (!snippetSvc) {
      qWarning() << "SnippetCoreService not available";
      return;
    }

    auto textEdit = p_win->m_editor->getTextEdit();
    auto cursor = textEdit->textCursor();

    // Get selected text.
    const auto selectedText = cursor.selectedText();

    // Get indentation of current line.
    const auto blockText = cursor.block().text();
    QString indentation;
    for (int i = 0; i < blockText.size(); ++i) {
      if (blockText[i] == QLatin1Char(' ') || blockText[i] == QLatin1Char('\t')) {
        indentation.append(blockText[i]);
      } else {
        break;
      }
    }

    // Generate overrides from buffer identity.
    const auto overrides = generateSnippetOverrides2(p_win);

    // Call SnippetCoreService to expand the snippet.
    const auto result = snippetSvc->applySnippet(p_name, selectedText, indentation, overrides);
    const auto appliedText = result[QStringLiteral("text")].toString();
    const int cursorOffset = result[QStringLiteral("cursorOffset")].toInt();

    if (appliedText.isEmpty()) {
      p_win->showMessage(ViewWindow2::tr("Snippet (%1) not found").arg(p_name));
      return;
    }

    // Insert text atomically.
    cursor.beginEditBlock();
    cursor.removeSelectedText();
    const int beforePos = cursor.position();
    cursor.insertText(appliedText);
    cursor.setPosition(beforePos + cursorOffset);
    cursor.endEditBlock();
    textEdit->setTextCursor(cursor);

    p_win->m_editor->enterInsertModeIfApplicable();
    p_win->showMessage(ViewWindow2::tr("Snippet applied: %1").arg(p_name));
  }

  // Apply snippet with auto-detection or QuickSelector prompt.
  // If %name% detected under cursor, validates and applies. Otherwise, shows picker.
  template <typename _ViewWindow> static void applySnippetPrompt2(_ViewWindow *p_win) {
    if (p_win->m_editor->isReadOnly()) {
      qWarning() << "failed to apply snippet to a read-only buffer";
      return;
    }

    auto *snippetSvc = p_win->getServices().template get<SnippetCoreService>();
    if (!snippetSvc) {
      qWarning() << "SnippetCoreService not available";
      return;
    }

    QString snippetName;

    auto textEdit = p_win->m_editor->getTextEdit();
    if (!textEdit->hasSelection()) {
      // Try to detect %name% symbol under cursor.
      auto symbolResult = detectSnippetSymbol2(p_win);
      if (symbolResult.found) {
        // Validate snippet exists BEFORE removing symbol from document.
        const auto snippetObj = snippetSvc->getSnippet(symbolResult.name);
        if (snippetObj.isEmpty()) {
          p_win->showMessage(ViewWindow2::tr("Snippet (%1) not found").arg(symbolResult.name));
          return;
        }

        // Remove the %name% symbol from document.
        auto cursor = textEdit->textCursor();
        const int blockPos = cursor.block().position();
        cursor.setPosition(blockPos + symbolResult.start);
        cursor.setPosition(blockPos + symbolResult.end, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        textEdit->setTextCursor(cursor);

        snippetName = symbolResult.name;
      }
    }

    if (snippetName.isEmpty()) {
      // Prompt for snippet selection via QuickSelector.
      snippetName = promptForSnippet2(p_win);
    }

    if (!snippetName.isEmpty()) {
      applySnippetByName2(p_win, snippetName);
    }
  }

  // Prompt user to select a snippet via QuickSelector floating widget.
  template <typename _ViewWindow> static QString promptForSnippet2(_ViewWindow *p_win) {
    auto *snippetSvc = p_win->getServices().template get<SnippetCoreService>();
    if (!snippetSvc) {
      return QString();
    }

    const auto snippets = snippetSvc->listSnippets();
    if (snippets.isEmpty()) {
      p_win->showMessage(ViewWindow2::tr("Snippet not available"));
      return QString();
    }

    QVector<QuickSelectorItem> items;
    for (const auto &snipVal : snippets) {
      const auto snip = snipVal.toObject();
      items.push_back(QuickSelectorItem(snip[QStringLiteral("name")].toString(),
                                        snip[QStringLiteral("name")].toString(),
                                        snip[QStringLiteral("description")].toString(),
                                        snip[QStringLiteral("shortcut")].toString()));
    }

    // Ownership will be transferred to showFloatingWidget().
    auto *themeSvc = p_win->getServices().template get<ThemeService>();
    auto selector =
        new QuickSelector(themeSvc, ViewWindow2::tr("Select Snippet"), items, true, p_win);
    auto ret = p_win->showFloatingWidget(selector);
    return ret.toString();
  }
};
} // namespace vnotex

#endif
