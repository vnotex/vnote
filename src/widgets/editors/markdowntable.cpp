#include "markdowntable.h"

#include <QTextEdit>
#include <QDebug>

using namespace vnotex;

void MarkdownTable::Cell::clear()
{
    m_offset = -1;
    m_length = 0;
    m_text.clear();
    m_formattedText.clear();
    m_cursorCoreOffset = -1;
    m_deleted = false;
}

bool MarkdownTable::Row::isValid() const
{
    return !m_cells.isEmpty();
}

void MarkdownTable::Row::clear()
{
    m_block = QTextBlock();
    m_preText.clear();
    m_cells.clear();
}

QString MarkdownTable::Row::toString() const
{
    QString cells;
    for (auto & cell : m_cells) {
        cells += QString(" (%1, %2 [%3])").arg(cell.m_offset)
                                         .arg(cell.m_length)
                                         .arg(cell.m_text);
    }

    return QString("row %1 %2").arg(m_block.blockNumber()).arg(cells);
}

int MarkdownTable::s_spaceWidth = -1;

int MarkdownTable::s_minusWidth = -1;

int MarkdownTable::s_colonWidth = -1;

int MarkdownTable::s_defaultDelimiterWidth = -1;

const QString MarkdownTable::c_defaultDelimiter = "---";

const QChar MarkdownTable::c_borderChar = '|';

enum
{
    HeaderRowIndex = 0,
    DelimiterRowIndex = 1
};

MarkdownTable::MarkdownTable(QTextEdit *p_textEdit, const vte::peg::TableBlock &p_block)
    : m_textEdit(p_textEdit)
{
    parseTableBlock(p_block);
}

MarkdownTable::MarkdownTable(QTextEdit *p_textEdit, int p_bodyRow, int p_col, Alignment p_alignment)
    : m_textEdit(p_textEdit),
      m_isNew(true)
{

}

bool MarkdownTable::isValid() const
{
    return header() && header()->isValid() && delimiter() && delimiter()->isValid();
}

void MarkdownTable::format()
{
    if (!isValid()) {
        return;
    }

    QTextCursor cursor = m_editor->getTextEdit()->textCursor();
    int curRowIdx = cursor.blockNumber() - m_rows[0].m_block.blockNumber();
    int curPib = -1;
    if (curRowIdx < 0 || curRowIdx >= m_rows.size()) {
        curRowIdx = -1;
    } else {
        curPib = cursor.positionInBlock();
    }

    int nrCols = calculateColumnCount();
    pruneColumns(nrCols);

    for (int i = 0; i < nrCols; ++i) {
        formatColumn(i, curRowIdx, curPib);
    }
}

void MarkdownTable::write()
{

}

void MarkdownTable::parseTableBlock(const vte::peg::TableBlock &p_block)
{
    auto doc = m_textEdit->document();

    QTextBlock block = doc->findBlock(p_block.m_startPos);
    if (!block.isValid()) {
        return;
    }

    int lastBlockNumber = doc->findBlock(p_block.m_endPos - 1).blockNumber();
    if (lastBlockNumber == -1) {
        return;
    }

    const QVector<int> &borders = p_block.m_borders;
    if (borders.isEmpty()) {
        return;
    }

    int numRows = lastBlockNumber - block.blockNumber() + 1;
    if (numRows <= DelimiterRowIndex) {
        return;
    }

    initWidths(block, borders[0]);

    int borderIdx = 0;
    m_rows.reserve(numRows);
    for (int i = 0; i < numRows; ++i) {
        m_rows.append(Row());
        if (!parseRow(block, borders, borderIdx, m_rows.last())) {
            clear();
            return;
        }

        qDebug() << "row" << i << m_rows.last().toString();

        block = block.next();
    }
}

void MarkdownTable::clear()
{
    m_rows.clear();
}

void MarkdownTable::initWidths(const QTextBlock &p_block, int p_borderPos)
{
    if (s_spaceWidth != -1) {
        return;
    }

    QFont font = m_textEdit->font();
    int pib = p_borderPos - p_block.position();
    auto fmts = p_block.layout()->formats();
    for (const auto &fmt : fmts) {
        if (fmt.start <= pib && fmt.start + fmt.length > pib) {
            // Hit.
            if (!fmt.format.fontFamily().isEmpty()) {
                font = fmt.format.font();
                break;
            }
        }
    }

    QFontMetrics fm(font);
    s_spaceWidth = fm.width(' ');
    s_minusWidth = fm.width('-');
    s_colonWidth = fm.width(':');
    s_defaultDelimiterWidth = fm.width(c_defaultDelimiter);

    qDebug() << "smart table widths" << font.family() << s_spaceWidth << s_minusWidth << s_colonWidth << s_defaultDelimiterWidth;
    if (s_spaceWidth != s_minusWidth || s_spaceWidth != s_colonWidth) {
        qWarning() << "it looks like that non-mono font is used for table, smart table may not work well" << font.family();
    }
}

bool MarkdownTable::parseRow(const QTextBlock &p_block,
                             const QVector<int> &p_borders,
                             int &p_borderIdx,
                             Row &p_row) const
{
    if (!p_block.isValid() || p_borderIdx >= p_borders.size()) {
        return false;
    }

    p_row.m_block = p_block;

    QString text = p_block.text();
    int startPos = p_block.position();
    int endPos = startPos + text.length();

    if (p_borders[p_borderIdx] < startPos
        || p_borders[p_borderIdx] >= endPos) {
        return false;
    }

    // Get pre text.
    int firstCellOffset = p_borders[p_borderIdx] - startPos;
    if (text[firstCellOffset] != c_borderChar) {
        return false;
    }
    p_row.m_preText = text.left(firstCellOffset);

    for (; p_borderIdx < p_borders.size(); ++p_borderIdx) {
        int border = p_borders[p_borderIdx];
        if (border >= endPos) {
            break;
        }

        int offset = border - startPos;
        if (text[offset] != c_borderChar) {
            return false;
        }

        int nextIdx = p_borderIdx + 1;
        if (nextIdx >= p_borders.size() || p_borders[nextIdx] >= endPos) {
            // The last border of this row.
            ++p_borderIdx;
            break;
        }

        int nextOffset = p_borders[nextIdx] - startPos;
        if (text[nextOffset] != c_borderChar) {
            return false;
        }

        // Got one cell.
        Cell cell;
        cell.m_offset = offset;
        cell.m_length = nextOffset - offset;
        cell.m_text = text.mid(cell.m_offset, cell.m_length);

        p_row.m_cells.append(cell);
    }

    return true;
}

const MarkdownTable::Row *MarkdownTable::header() const
{
    if (m_rows.size() <= HeaderRowIndex) {
        return nullptr;
    }

    return &m_rows[HeaderRowIndex];
}

const MarkdownTable::Row *MarkdownTable::delimiter() const
{
    if (m_rows.size() <= DelimiterRowIndex) {
        return nullptr;
    }

    return &m_rows[DelimiterRowIndex];
}

int MarkdownTable::calculateColumnCount() const
{
    // We use the width of the header as the width of the table.
    // With this, we could add or remove one column by just changing the header row.
    return header()->m_cells.size();
}

void MarkdownTable::pruneColumns(int p_nrCols)
{
    for (auto &row : m_rows) {
        for (int i = p_nrCols; i < row.m_cells.size(); ++i) {
            row.m_cells[i].m_deleted = true;
        }
    }
}

void MarkdownTable::formatColumn(int p_idx, int p_cursorRowIdx, int p_cursorPib)
{
    QVector<CellInfo> cells;
    int targetWidth = 0;
    fetchCellInfoOfColumn(p_idx, cells, targetWidth);

    // Get the alignment of this column.
    const Alignment align = getColumnAlignment(p_idx);

    // Calculate the formatted text of each cell.
    for (int rowIdx = 0; rowIdx < cells.size(); ++rowIdx) {
        auto & info = cells[rowIdx];
        auto & row = m_rows[rowIdx];
        if (row.m_cells.size() <= p_idx) {
            row.m_cells.resize(p_idx + 1);
        }
        auto & cell = row.m_cells[p_idx];
        Q_ASSERT(cell.m_formattedText.isEmpty());
        Q_ASSERT(cell.m_cursorCoreOffset == -1);

        // Record the cursor position.
        if (rowIdx == p_cursorRowIdx) {
            if (cell.m_offset <= p_cursorPib && cell.m_offset + cell.m_length > p_cursorPib) {
                // Cursor in this cell.
                int offset = p_cursorPib - cell.m_offset;
                offset = offset - info.m_coreOffset;
                if (offset > info.m_coreLength) {
                    offset = info.m_coreLength;
                } else if (offset < 0) {
                    offset = 0;
                }

                cell.m_cursorCoreOffset = offset;
            }
        }

        if (isDelimiterRow(rowIdx)) {
            if (!isDelimiterCellWellFormatted(cell, info, targetWidth)) {
                QString core;
                // Round to 1 when above 0.5 approximately.
                int delta = m_minusWidth / 2;
                switch (align) {
                case Alignment::None:
                    core = QString((targetWidth + delta) / m_minusWidth, '-');
                    break;

                case Alignment::Left:
                    core = ":";
                    core += QString((targetWidth - m_colonWidth + delta) / m_minusWidth, '-');
                    break;

                case Alignment::Center:
                    core = ":";
                    core += QString((targetWidth - 2 * m_colonWidth + delta) / m_minusWidth, '-');
                    core += ":";
                    break;

                case Alignment::Right:
                    core = QString((targetWidth - m_colonWidth + delta) / m_minusWidth, '-');
                    core += ":";
                    break;

                default:
                    Q_ASSERT(false);
                    break;
                }

                cell.m_formattedText = generateFormattedText(core, 0);
            }
        } else {
            Alignment fakeAlign = align;
            if (fakeAlign == Alignment::None) {
                // For Alignment::None, we make the header align center while
                // content cells align left.
                if (isHeaderRow(rowIdx)) {
                    fakeAlign = Alignment::Center;
                } else {
                    fakeAlign = Alignment::Left;
                }
            }

            if (!isCellWellFormatted(row, cell, info, targetWidth, fakeAlign)) {
                QString core = cell.m_text.mid(info.m_coreOffset, info.m_coreLength);
                int nr = (targetWidth - info.m_coreWidth + m_spaceWidth / 2) / m_spaceWidth;
                cell.m_formattedText = generateFormattedText(core, nr, fakeAlign);

                // For cells crossing lines and having spaces at the end of one line,
                // Qt will collapse those spaces, which make it not well formatted.
                if (cell.m_text == cell.m_formattedText) {
                    cell.m_formattedText.clear();
                }
            }
        }
    }
}

void MarkdownTable::fetchCellInfoOfColumn(int p_idx,
                                          QVector<CellInfo> &p_cellsInfo,
                                          int &p_targetWidth) const
{
    p_targetWidth = m_defaultDelimiterWidth;
    p_cellsInfo.resize(m_rows.size());

    // Fetch the trimmed core content and its width.
    for (int i = 0; i < m_rows.size(); ++i) {
        auto & row = m_rows[i];
        auto & info = p_cellsInfo[i];

        if (row.m_cells.size() <= p_idx) {
            // Need to add a new cell later.
            continue;
        }

        // Get the info of this cell.
        const auto & cell = row.m_cells[p_idx];
        int first = 1, last = cell.m_length - 1;
        for (; first <= last; ++first) {
            if (cell.m_text[first] != ' ') {
                // Found the core content.
                info.m_coreOffset = first;
                break;
            }
        }

        if (first > last) {
            // Empty cell.
            continue;
        }

        for (; last >= first; --last) {
            if (cell.m_text[last] != ' ') {
                // Found the last of core content.
                info.m_coreLength = last - first + 1;
                break;
            }
        }

        // Calculate the core width.
        info.m_coreWidth = calculateTextWidth(row.m_block,
                                              cell.m_offset + info.m_coreOffset,
                                              info.m_coreLength);
        // Delimiter row's width should not be considered.
        if (info.m_coreWidth > p_targetWidth && !isDelimiterRow(i)) {
            p_targetWidth = info.m_coreWidth;
        }
    }
}
