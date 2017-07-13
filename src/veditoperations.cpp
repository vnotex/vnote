#include <QTextCursor>
#include <QTextDocument>
#include <QFontMetrics>
#include "vedit.h"
#include "veditoperations.h"
#include "vconfigmanager.h"
#include "utils/vutils.h"

extern VConfigManager vconfig;

VEditOperations::VEditOperations(VEdit *p_editor, VFile *p_file)
    : QObject(p_editor), m_editor(p_editor), m_file(p_file),
      m_editConfig(&p_editor->getConfig()), m_vim(NULL)
{
    connect(m_editor, &VEdit::configUpdated,
            this, &VEditOperations::handleEditConfigUpdated);

    if (m_editConfig->m_enableVimMode) {
        m_vim = new VVim(m_editor);

        connect(m_vim, &VVim::modeChanged,
                this, &VEditOperations::handleVimModeChanged);
        connect(m_vim, &VVim::vimMessage,
                this, &VEditOperations::statusMessage);
        connect(m_vim, &VVim::vimStatusUpdated,
                this, &VEditOperations::vimStatusUpdated);
    }
}

void VEditOperations::insertTextAtCurPos(const QString &p_text)
{
    m_editor->insertPlainText(p_text);
}

VEditOperations::~VEditOperations()
{
}

void VEditOperations::updateCursorLineBg()
{
    if (m_editConfig->m_enableVimMode) {
        switch (m_vim->getMode()) {
        case VimMode::Normal:
            m_editConfig->m_cursorLineBg = QColor(vconfig.getEditorVimNormalBg());
            break;

        case VimMode::Insert:
            m_editConfig->m_cursorLineBg = QColor(vconfig.getEditorVimInsertBg());
            break;

        case VimMode::Visual:
        case VimMode::VisualLine:
            m_editConfig->m_cursorLineBg = QColor(vconfig.getEditorVimVisualBg());
            break;

        case VimMode::Replace:
            m_editConfig->m_cursorLineBg = QColor(vconfig.getEditorVimReplaceBg());
            break;

        default:
            V_ASSERT(false);
            break;
        }
    } else {
        m_editConfig->m_cursorLineBg = QColor(vconfig.getEditorCurrentLineBg());
    }

    m_editor->highlightCurrentLine();
}

void VEditOperations::handleEditConfigUpdated()
{
    // Reset to Normal mode.
    if (m_vim) {
        m_vim->setMode(VimMode::Normal);
    }

    updateCursorLineBg();
}

void VEditOperations::handleVimModeChanged(VimMode p_mode)
{
    // Only highlight current visual line in Insert mode.
    m_editConfig->m_highlightWholeBlock = (p_mode != VimMode::Insert);

    updateCursorLineBg();
}

void VEditOperations::requestUpdateVimStatus()
{
    emit vimStatusUpdated(m_vim);
}
