#include <QTextCursor>
#include <QTextDocument>
#include <QFontMetrics>
#include "veditor.h"
#include "veditoperations.h"
#include "vconfigmanager.h"
#include "utils/vutils.h"

extern VConfigManager *g_config;

VEditOperations::VEditOperations(VEditor *p_editor, VFile *p_file)
    : QObject(p_editor->getEditor()),
      m_editor(p_editor),
      m_file(p_file),
      m_editConfig(&p_editor->getConfig()),
      m_vim(NULL)
{
    connect(m_editor->object(), &VEditorObject::configUpdated,
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

void VEditOperations::insertText(const QString &p_text)
{
    m_editor->insertPlainTextW(p_text);
}

VEditOperations::~VEditOperations()
{
}

void VEditOperations::updateCursorLineBg()
{
    if (m_editConfig->m_enableVimMode) {
        switch (m_vim->getMode()) {
        case VimMode::Normal:
            m_editConfig->m_cursorLineBg = QColor(g_config->getEditorVimNormalBg());
            break;

        case VimMode::Insert:
            m_editConfig->m_cursorLineBg = QColor(g_config->getEditorVimInsertBg());
            break;

        case VimMode::Visual:
        case VimMode::VisualLine:
            m_editConfig->m_cursorLineBg = QColor(g_config->getEditorVimVisualBg());
            break;

        case VimMode::Replace:
            m_editConfig->m_cursorLineBg = QColor(g_config->getEditorVimReplaceBg());
            break;

        default:
            V_ASSERT(false);
            break;
        }
    } else {
        m_editConfig->m_cursorLineBg = QColor(g_config->getEditorCurrentLineBg());
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

void VEditOperations::setVimMode(VimMode p_mode)
{
    if (m_vim && m_editConfig->m_enableVimMode) {
        m_vim->setMode(p_mode);
    }
}

VVim *VEditOperations::getVim() const
{
    return m_vim;
}
