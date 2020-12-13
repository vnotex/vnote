#ifndef TEXTVIEWWINDOWHELPER_H
#define TEXTVIEWWINDOWHELPER_H

#include <QFileInfo>

#include <vtextedit/texteditorconfig.h>
#include <core/texteditorconfig.h>
#include <core/configmgr.h>
#include <utils/widgetutils.h>

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
                                                                            const QString &p_themeFile,
                                                                            const QString &p_syntaxTheme)
        {
            auto editorConfig = QSharedPointer<vte::TextEditorConfig>::create();

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
        static void handleFindNext(_ViewWindow *p_win, const QString &p_text, FindOptions p_options)
        {
            const auto result = p_win->m_editor->findText(p_text, toEditorFindFlags(p_options));
            p_win->showFindResult(p_text, result.m_totalMatches, result.m_currentMatchIndex);
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
        static void handleFindAndReplaceWidgetClosed(_ViewWindow *p_win)
        {
            p_win->m_editor->clearIncrementalSearchHighlight();
            p_win->m_editor->clearSearchHighlight();
        }
    };
}

#endif
