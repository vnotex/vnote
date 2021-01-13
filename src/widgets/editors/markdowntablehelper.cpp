#include "markdowntablehelper.h"

#include <QTimer>
#include <QTextEdit>
#include <QTextCursor>

#include <vtextedit/vtexteditor.h>
#include <vtextedit/vtextedit.h>

#include <core/configmgr.h>
#include <core/markdowneditorconfig.h>
#include <core/editorconfig.h>

using namespace vnotex;

MarkdownTableHelper::MarkdownTableHelper(vte::VTextEditor *p_editor, QObject *p_parent)
    : QObject(p_parent),
      m_editor(p_editor)
{
}

bool MarkdownTableHelper::isSmartTableEnabled() const
{
    return ConfigMgr::getInst().getEditorConfig().getMarkdownEditorConfig().getSmartTableEnabled();
}

QTimer *MarkdownTableHelper::getTimer()
{
    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setSingleShot(true);
        m_timer->setInterval(ConfigMgr::getInst().getEditorConfig().getMarkdownEditorConfig().getSmartTableInterval());
        connect(m_timer, &QTimer::timeout,
                this, &MarkdownTableHelper::formatTable);

        connect(m_editor->getTextEdit(), &QTextEdit::cursorPositionChanged,
                this, [this]() {
                    if (m_timer->isActive()) {
                        // Defer the formatting.
                        m_timer->start();
                    }
                });
    }

    return m_timer;
}

void MarkdownTableHelper::formatTable()
{
    if (!isSmartTableEnabled()) {
        return;
    }

    if (!m_block.isValid()) {
        return;
    }


    MarkdownTable table(m_editor->getTextEdit(), m_block);
    if (!table.isValid()) {
        return;
    }

    table.format();

    table.write();
}

void MarkdownTableHelper::updateTableBlocks(const QVector<vte::peg::TableBlock> &p_blocks)
{
    if (!isSmartTableEnabled()) {
        return;
    }

    getTimer()->stop();

    if (m_editor->isReadOnly() || !m_editor->isModified()) {
        return;
    }

    int idx = currentCursorTableBlock(p_blocks);
    if (idx == -1) {
        return;
    }

    m_block = p_blocks[idx];
    getTimer()->start();
}

int MarkdownTableHelper::currentCursorTableBlock(const QVector<vte::peg::TableBlock> &p_blocks) const
{
    // Binary search.
    int curPos = m_editor->getTextEdit()->textCursor().position();

    int first = 0, last = p_blocks.size() - 1;
    while (first <= last) {
        int mid = (first + last) / 2;
        const auto &block = p_blocks[mid];
        if (block.m_startPos <= curPos && block.m_endPos >= curPos) {
            return mid;
        }

        if (block.m_startPos > curPos) {
            last = mid - 1;
        } else {
            first = mid + 1;
        }
    }

    return -1;
}

void MarkdownTableHelper::insertTable(int p_bodyRow, int p_col, Alignment p_alignment)
{
    MarkdownTable table(m_editor->getTextEdit(), p_bodyRow, p_col, p_alignment);
    if (!table.isValid()) {
        return;
    }

    table.write();
}
