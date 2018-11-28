#include "vtablehelper.h"

#include "veditor.h"
#include "vtable.h"

VTableHelper::VTableHelper(VEditor *p_editor, QObject *p_parent)
    : QObject(p_parent),
      m_editor(p_editor)
{
}

void VTableHelper::updateTableBlocks(const QVector<VTableBlock> &p_blocks)
{
    if (m_editor->isReadOnlyW() || !m_editor->isModified()) {
        return;
    }

    int idx = currentCursorTableBlock(p_blocks);
    if (idx == -1) {
        return;
    }

    VTable table(m_editor, p_blocks[idx]);
    if (!table.isValid()) {
        return;
    }

    table.format();

    table.write();
}

int VTableHelper::currentCursorTableBlock(const QVector<VTableBlock> &p_blocks) const
{
    // Binary search.
    int curPos = m_editor->textCursorW().position();

    int first = 0, last = p_blocks.size() - 1;
    while (first <= last) {
        int mid = (first + last) / 2;
        const VTableBlock &block = p_blocks[mid];
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
