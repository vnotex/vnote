#ifndef TEXTVIEWWINDOWHELPER_H
#define TEXTVIEWWINDOWHELPER_H

#include <QFileInfo>
#include <QTextCursor>
#include <QRegularExpression>
#include <QTextBlock>
#include <QSharedPointer>

#include <vtextedit/texteditorconfig.h>
#include <core/texteditorconfig.h>
#include <core/configmgr.h>
#include <utils/widgetutils.h>
#include <snippet/snippetmgr.h>
#include <search/searchtoken.h>

#include "quickselector.h"

namespace vte
{
    class ViConfig;
}

namespace vnotex
{
    class TextEditorConfig;

    // Abstract some common logics for TextViewWindow and MarkdownViewWindow.
    class TextViewWindowHelper
    {
    public:
        TextViewWindowHelper() = delete;

        template <typename _ViewWindow>
        static void connectEditor(_ViewWindow *p_win)
        {
            auto editor = p_win->m_editor;
            p_win->connect(editor, &vte::VTextEditor::focusIn,
                           p_win, [p_win]() {
                               emit p_win->focused(p_win);
                           });

            p_win->connect(editor->getTextEdit(), &vte::VTextEdit::contentsChanged,
                           p_win, [p_win, editor]() {
                               if (p_win->m_propogateEditorToBuffer) {
                                   p_win->getBuffer()->setModified(editor->isModified());
                                   p_win->getBuffer()->invalidateContent(
                                       p_win, [p_win](int p_revision) {
                                           p_win->setBufferRevisionAfterInvalidation(p_revision);
                                       });
                               }
                           });
        }

        template <typename _ViewWindow>
        static void handleBufferChanged(_ViewWindow *p_win)
        {
            p_win->m_propogateEditorToBuffer = false;
            p_win->syncEditorFromBuffer();
            p_win->m_propogateEditorToBuffer = true;

            emit p_win->statusChanged();
            emit p_win->modeChanged();
        }

        static QSharedPointer<vte::TextEditorConfig> createTextEditorConfig(const TextEditorConfig &p_config,
                                                                            const QSharedPointer<vte::ViConfig> &p_viConfig,
                                                                            const QString &p_themeFile,
                                                                            const QString &p_syntaxTheme,
                                                                            LineEndingPolicy p_lineEndingPolicy)
        {
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

        static vte::FindFlags toEditorFindFlags(FindOptions p_options)
        {
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
        static void handleFindTextChanged(_ViewWindow *p_win, const QString &p_text, FindOptions p_options)
        {
            if (p_options & FindOption::IncrementalSearch) {
                p_win->m_editor->peekText(p_text, toEditorFindFlags(p_options));
            }
        }

        template <typename _ViewWindow>
        static void handleFindNext(_ViewWindow *p_win, const QStringList &p_texts, FindOptions p_options)
        {
            const auto result = p_win->m_editor->findText(p_texts, toEditorFindFlags(p_options));
            p_win->showFindResult(p_texts, result.m_totalMatches, result.m_currentMatchIndex);
        }

        template <typename _ViewWindow>
        static void handleReplace(_ViewWindow *p_win, const QString &p_text, FindOptions p_options, const QString &p_replaceText)
        {
            const auto result = p_win->m_editor->replaceText(p_text, toEditorFindFlags(p_options), p_replaceText);
            p_win->showReplaceResult(p_text, result.m_totalMatches);
        }

        template <typename _ViewWindow>
        static void handleReplaceAll(_ViewWindow *p_win, const QString &p_text, FindOptions p_options, const QString &p_replaceText)
        {
            const auto result = p_win->m_editor->replaceAll(p_text, toEditorFindFlags(p_options), p_replaceText);
            p_win->showReplaceResult(p_text, result.m_totalMatches);
        }

        template <typename _ViewWindow>
        static void clearSearchHighlights(_ViewWindow *p_win)
        {
            p_win->m_editor->clearIncrementalSearchHighlight();
            p_win->m_editor->clearSearchHighlight();
        }

        template <typename _ViewWindow>
        static void applySnippet(_ViewWindow *p_win, const QString &p_name)
        {
            if (p_win->m_editor->isReadOnly() || p_name.isEmpty()) {
                qWarning() << "failed to apply snippet" << p_name << "to a read-only buffer";
                return;
            }

            SnippetMgr::getInst().applySnippet(p_name,
                                               p_win->m_editor->getTextEdit(),
                                               SnippetMgr::generateOverrides(p_win->getBuffer()));
            p_win->m_editor->enterInsertModeIfApplicable();
            p_win->showMessage(vnotex::ViewWindow::tr("Snippet applied: %1").arg(p_name));
        }

        template <typename _ViewWindow>
        static void applySnippet(_ViewWindow *p_win)
        {
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
                    cursor.setPosition(block.position() + idx + match.capturedLength(0), QTextCursor::KeepAnchor);
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

        template <typename _ViewWindow>
        static QString promptForSnippet(_ViewWindow *p_win)
        {
            const auto snippets = SnippetMgr::getInst().getSnippets();
            if (snippets.isEmpty()) {
                p_win->showMessage(vnotex::ViewWindow::tr("Snippet not available"));
                return QString();
            }

            QVector<QuickSelectorItem> items;
            for (const auto &snip : snippets) {
                items.push_back(QuickSelectorItem(snip->getName(),
                                                  snip->getName(),
                                                  snip->getDescription(),
                                                  snip->getShortcutString()));
            }

            // Ownership will be transferred to showFloatingWidget().
            auto selector = new QuickSelector(vnotex::ViewWindow::tr("Select Snippet"),
                                              items,
                                              true,
                                              p_win);
            auto ret = p_win->showFloatingWidget(selector);
            return ret.toString();
        }

        template <typename _ViewWindow>
        static QPoint getFloatingWidgetPosition(_ViewWindow *p_win)
        {
            auto textEdit = p_win->m_editor->getTextEdit();
            auto localPos = textEdit->cursorRect().bottomRight();
            if (!textEdit->rect().contains(localPos)) {
                localPos = QPoint(5, 5);
            }
            return textEdit->mapToGlobal(localPos);
        }

        template <typename _ViewWindow>
        static void findTextBySearchToken(_ViewWindow *p_win, const QSharedPointer<SearchToken> &p_token, int p_currentMatchLine)
        {
            const auto patterns = p_token->toPatterns();
            p_win->updateLastFindInfo(patterns.first, patterns.second);
            const auto result = p_win->m_editor->findText(patterns.first, toEditorFindFlags(patterns.second), 0, -1, p_currentMatchLine);
            p_win->showFindResult(patterns.first, result.m_totalMatches, result.m_currentMatchIndex);
        }

        static ViewWindow::WordCountInfo calculateWordCountInfo(const QString &p_text)
        {
            ViewWindow::WordCountInfo info;

            // Char without spaces.
            int cns = 0;
            int wc = 0;
            // Remove th ending new line.
            int cc = p_text.size();
            // 0 - not in word;
            // 1 - in English word;
            // 2 - in non-English word;
            int state = 0;

            for (int i = 0; i < cc; ++i) {
                QChar ch = p_text[i];
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

            info.m_wordCount = wc;
            info.m_charWithoutSpaceCount = cns;
            info.m_charWithSpaceCount = cc;
            return info;
        }
    };
}

#endif
