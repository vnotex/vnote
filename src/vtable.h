#ifndef VTABLE_H
#define VTABLE_H

#include <QTextBlock>

#include "markdownhighlighterdata.h"

class VEditor;

class VTable
{
public:
    enum Alignment {
        None,
        Left,
        Center,
        Right
    };

    VTable(VEditor *p_editor, const VTableBlock &p_block);

    VTable(VEditor *p_editor, int p_nrBodyRow, int p_nrCol, VTable::Alignment p_alignment);

    bool isValid() const;

    void format();

    // Write a formatted table.
    void write();

private:
    struct Cell
    {
        Cell()
            : m_offset(-1),
              m_length(0),
              m_cursorCoreOffset(-1)
        {
        }

        void clear()
        {
            m_offset = -1;
            m_length = 0;
            m_text.clear();
            m_formattedText.clear();
            m_cursorCoreOffset = -1;
        }

        // Start offset within block, including the starting border |.
        int m_offset;

        // Length of this cell, till next border |.
        int m_length;

        // Text like "|  vnote  ".
        QString m_text;

        // Formatted text, such as "|   vnote   ".
        // It is empty if it does not need formatted.
        QString m_formattedText;

        // If cursor is within this cell, this will not be -1.
        int m_cursorCoreOffset;
    };

    struct Row
    {
        Row()
        {
        }

        bool isValid() const
        {
            return !m_cells.isEmpty();
        }

        void clear()
        {
            m_block = QTextBlock();
            m_preText.clear();
            m_cells.clear();
        }

        QString toString() const
        {
            QString cells;
            for (auto & cell : m_cells) {
                cells += QString(" (%1, %2 [%3])").arg(cell.m_offset)
                                                 .arg(cell.m_length)
                                                 .arg(cell.m_text);
            }

            return QString("row %1 %2").arg(m_block.blockNumber()).arg(cells);
        }

        QTextBlock m_block;
        // Text before table row.
        QString m_preText;
        QVector<Cell> m_cells;
    };

    // Used to hold info about a cell when formatting a column.
    struct CellInfo
    {
        CellInfo()
            : m_coreOffset(0),
              m_coreLength(0),
              m_coreWidth(0)
        {
        }

        // The offset of the core content within the cell.
        // Will be 0 if it is an empty cell.
        int m_coreOffset;

        // The length of the core content.
        // Will be 0 if it is an empty cell.
        int m_coreLength;

        // Pixel width of the core content.
        int m_coreWidth;
    };

    void parseFromTableBlock(const VTableBlock &p_block);

    void clear();

    bool parseOneRow(const QTextBlock &p_block,
                     const QVector<int> &p_borders,
                     int &p_borderIdx,
                     Row &p_row) const;

    int calculateColumnCount() const;

    // When called with i, the (i - 1) column must have been formatted.
    void formatOneColumn(int p_idx, int p_cursorRowIdx, int p_cursorPib);

    void fetchCellInfoOfColumn(int p_idx,
                              QVector<CellInfo> &p_cellsInfo,
                              int &p_targetWidth) const;

    void calculateBasicWidths(const QTextBlock &p_block, int p_borderPos);

    int calculateTextWidth(const QTextBlock &p_block, int p_pib, int p_length) const;

    bool isHeaderRow(int p_idx) const;

    bool isDelimiterRow(int p_idx) const;

    // @p_nrSpaces: number of spaces to fill core content.
    QString generateFormattedText(const QString &p_core,
                                  int p_nrSpaces = 0,
                                  Alignment p_align = Alignment::Left) const;

    VTable::Alignment getColumnAlignment(int p_idx) const;

    bool isDelimiterCellWellFormatted(const Cell &p_cell,
                                      const CellInfo &p_info,
                                      int p_targetWidth) const;

    bool isCellWellFormatted(const Row &p_row,
                             const Cell &p_cell,
                             const CellInfo &p_info,
                             int p_targetWidth,
                             Alignment p_align) const;

    void writeExist();

    void writeNonExist();

    VTable::Row *header() const;

    VTable::Row *delimiter() const;

    VEditor *m_editor;

    // Whether this table exist already.
    bool m_exist;

    // Header, delimiter, and body.
    QVector<Row> m_rows;

    int m_spaceWidth;
    int m_minusWidth;
    int m_colonWidth;
    int m_defaultDelimiterWidth;

    static const QString c_defaultDelimiter;

    static const QChar c_borderChar;
};

#endif // VTABLE_H
