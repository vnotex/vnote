#ifndef MARKDOWNTABLEHELPER_H
#define MARKDOWNTABLEHELPER_H

#include <QObject>

#include "markdowntable.h"

class QTimer;

namespace vte
{
    class VTextEditor;
}

namespace vnotex
{
    class MarkdownTableHelper : public QObject
    {
        Q_OBJECT
    public:
        MarkdownTableHelper(vte::VTextEditor *p_editor, QObject *p_parent = nullptr);

        void insertTable(int p_bodyRow, int p_col, Alignment p_alignment);

    public slots:
        void updateTableBlocks(const QVector<vte::peg::TableBlock> &p_blocks);

    private:
        // Return the block index which contains the cursor.
        int currentCursorTableBlock(const QVector<vte::peg::TableBlock> &p_blocks) const;

        void formatTable();

        bool isSmartTableEnabled() const;

        QTimer *getTimer();

        vte::VTextEditor *m_editor = nullptr;

        // Use getTimer() to access.
        QTimer *m_timer = nullptr;

        vte::peg::TableBlock m_block;
    };
}

#endif // MARKDOWNTABLEHELPER_H
