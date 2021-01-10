#ifndef MARKDOWNTABLE_H
#define MARKDOWNTABLE_H

#include <QTextBlock>
#include <QVector>

#include <vtextedit/pegmarkdownhighlighterdata.h>

#include <core/global.h>

class QTextEdit;

namespace vnotex
{
    class MarkdownTable
    {
    public:
        MarkdownTable(QTextEdit *p_textEdit, const vte::peg::TableBlock &p_block);

        MarkdownTable(QTextEdit *p_textEdit, int p_bodyRow, int p_col, Alignment p_alignment);

        bool isValid() const;

        void format();

        void write();

    private:
        struct Cell
        {
            void clear();

            // Start offset within block, including the starting border |.
            int m_offset = -1;

            // Length of this cell, till next border |.
            int m_length = 0;

            // Text like "|  vnote  ".
            QString m_text;

            // Formatted text, such as "|   vnote   ".
            // It is empty if it does not need formatted.
            QString m_formattedText;

            // If cursor is within this cell, this will not be -1.
            int m_cursorCoreOffset = -1;

            // Whether this cell need to be deleted.
            bool m_deleted = false;
        };

        struct Row
        {
            bool isValid() const;

            void clear();

            QString toString() const;

            QTextBlock m_block;

            // Text before (the first cell of) table row.
            QString m_preText;

            QVector<Cell> m_cells;
        };

        // Used to hold info about a cell when formatting a column.
        struct CellInfo
        {
            // The offset of the core content within the cell.
            // Will be 0 if it is an empty cell.
            int m_coreOffset = 0;

            // The length of the core content.
            // Will be 0 if it is an empty cell.
            int m_coreLength = 0;

            // Pixel width of the core content.
            qreal m_coreWidth = 0;
        };

        void parseTableBlock(const vte::peg::TableBlock &p_block);

        void clear();

        void initWidths(const QTextBlock &p_block, int p_borderPos);

        // Parse one row into @p_row and move @p_borderIdx forward.
        bool parseRow(const QTextBlock &p_block,
                      const QVector<int> &p_borders,
                      int &p_borderIdx,
                      Row &p_row) const;

        const Row *header() const;

        const Row *delimiter() const;

        int calculateColumnCount() const;

        // Prune columns beyond the header row that should be deleted.
        void pruneColumns(int p_nrCols);

        void formatColumn(int p_idx, int p_cursorRowIdx, int p_cursorPib);

        void fetchCellInfoOfColumn(int p_idx,
                                   int p_cursorRowIdx,
                                   int p_cursorPib,
                                   QVector<CellInfo> &p_cellsInfo,
                                   qreal &p_targetWidth) const;

        bool isHeaderRow(int p_idx) const;

        bool isDelimiterRow(int p_idx) const;

        qreal calculateTextWidth(const QTextBlock &p_block, int p_pib, int p_length) const;

        Alignment getColumnAlignment(int p_idx) const;

        bool isDelimiterCellWellFormatted(const Cell &p_cell,
                                          const CellInfo &p_info,
                                          qreal p_targetWidth) const;

        // @p_nrSpaces: number of spaces to fill core content.
        QString generateFormattedText(const QString &p_core,
                                      int p_nrSpaces,
                                      Alignment p_align = Alignment::Left) const;

        bool isCellWellFormatted(const Row &p_row,
                                 const Cell &p_cell,
                                 const CellInfo &p_info,
                                 int p_targetWidth,
                                 Alignment p_align) const;

        void writeTable();

        void writeNewTable();

        // Return -1 if it is an empty cell.
        static int fetchCoreOffset(const QString &p_cellText);

        QTextEdit *m_textEdit = nullptr;

        // Whether this table is a new table or not.
        bool m_isNew = false;

        // Header, delimiter, and body.
        QVector<Row> m_rows;

        static qreal s_spaceWidth;

        static qreal s_minusWidth;

        static qreal s_colonWidth;

        static qreal s_defaultDelimiterWidth;

        static const QString c_defaultDelimiter;

        static const QChar c_borderChar;
    };
}

#endif // MARKDOWNTABLE_H
