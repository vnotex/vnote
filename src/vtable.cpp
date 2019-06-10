#include "vtable.h"

#include <QTextDocument>
#include <QTextLayout>
#include <QDebug>

#include "veditor.h"

const QString VTable::c_defaultDelimiter = "---";

const QChar VTable::c_borderChar = '|';

enum { HeaderRowIndex = 0, DelimiterRowIndex = 1 };

VTable::VTable(VEditor *p_editor, const VTableBlock &p_block)
    : m_editor(p_editor),
      m_exist(true),
      m_spaceWidth(10),
      m_minusWidth(10),
      m_colonWidth(10),
      m_defaultDelimiterWidth(10)
{
    parseFromTableBlock(p_block);
}

VTable::VTable(VEditor *p_editor, int p_nrBodyRow, int p_nrCol, VTable::Alignment p_alignment)
    : m_editor(p_editor),
      m_exist(false),
      m_spaceWidth(10),
      m_minusWidth(10),
      m_colonWidth(10),
      m_defaultDelimiterWidth(10)
{
    Q_ASSERT(p_nrBodyRow >= 0 && p_nrCol > 0);
    m_rows.resize(p_nrBodyRow + 2);

    // PreText for each row.
    QString preText;
    QTextCursor cursor = m_editor->textCursorW();
    Q_ASSERT(cursor.atBlockEnd());
    if (!cursor.atBlockStart()) {
        preText = cursor.block().text();
    }

    QString core(c_defaultDelimiter);
    switch (p_alignment) {
    case Alignment::Left:
        core[0] = ':';
        break;

    case Alignment::Center:
        core[0] = ':';
        core[core.size() - 1] = ':';
        break;

    case Alignment::Right:
        core[core.size() - 1] = ':';
        break;

    default:
        break;
    }
    const QString delimiterCell = generateFormattedText(core, 0);
    const QString contentCell = generateFormattedText(QString(c_defaultDelimiter.size(), ' '), 0);

    for (int rowIdx = 0; rowIdx < m_rows.size(); ++rowIdx) {
        auto & row = m_rows[rowIdx];
        row.m_preText = preText;
        row.m_cells.resize(p_nrCol);

        const QString &content = isDelimiterRow(rowIdx) ? delimiterCell : contentCell;
        for (auto & cell : row.m_cells) {
            cell.m_text = content;
        }
    }
}

bool VTable::isValid() const
{
    return header() && header()->isValid()
           && delimiter() && delimiter()->isValid();
}

void VTable::parseFromTableBlock(const VTableBlock &p_block)
{
    clear();

    QTextDocument *doc = m_editor->documentW();

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

    calculateBasicWidths(block, borders[0]);

    int borderIdx = 0;
    m_rows.reserve(numRows);
    for (int i = 0; i < numRows; ++i) {
        m_rows.append(Row());
        if (!parseOneRow(block, borders, borderIdx, m_rows.last())) {
            clear();
            return;
        }

        block = block.next();
    }
}

bool VTable::parseOneRow(const QTextBlock &p_block,
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

void VTable::clear()
{
    m_rows.clear();
    m_spaceWidth = 0;
    m_minusWidth = 0;
    m_colonWidth = 0;
    m_defaultDelimiterWidth = 0;
}

void VTable::format()
{
    if (!isValid()) {
        return;
    }

    QTextCursor cursor = m_editor->textCursorW();
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
        formatOneColumn(i, curRowIdx, curPib);
    }
}

int VTable::calculateColumnCount() const
{
    // We use the width of the header as the width of the table.
    // With this, we could add or remove one column by just changing the header row.
    return header()->m_cells.size();
}

VTable::Row *VTable::header() const
{
    if (m_rows.size() <= HeaderRowIndex) {
        return NULL;
    }

    return const_cast<VTable::Row *>(&m_rows[HeaderRowIndex]);
}

VTable::Row *VTable::delimiter() const
{
    if (m_rows.size() <= DelimiterRowIndex) {
        return NULL;
    }

    return const_cast<VTable::Row *>(&m_rows[DelimiterRowIndex]);
}

void VTable::formatOneColumn(int p_idx, int p_cursorRowIdx, int p_cursorPib)
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

void VTable::fetchCellInfoOfColumn(int p_idx,
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

void VTable::calculateBasicWidths(const QTextBlock &p_block, int p_borderPos)
{
    QFont font;

    int pib = p_borderPos - p_block.position();
    QVector<QTextLayout::FormatRange> fmts = p_block.layout()->formats();
    for (const auto & fmt : fmts) {
        if (fmt.start <= pib && fmt.start + fmt.length > pib) {
            // Hit.
            if (!fmt.format.fontFamily().isEmpty()) {
                font = fmt.format.font();
                break;
            }
        }
    }

    QFontMetrics fm(font);
    m_spaceWidth = fm.width(' ');
    m_minusWidth = fm.width('-');
    m_colonWidth = fm.width(':');
    m_defaultDelimiterWidth = fm.width(c_defaultDelimiter);
}

int VTable::calculateTextWidth(const QTextBlock &p_block, int p_pib, int p_length) const
{
    // The block may cross multiple lines.
    int width = 0;
    QTextLayout *layout = p_block.layout();
    QTextLine line = layout->lineForTextPosition(p_pib);
    while (line.isValid()) {
        int lineEnd = line.textStart() + line.textLength();
        if (lineEnd >= p_pib + p_length) {
            // The last line.
            width += line.cursorToX(p_pib + p_length) - line.cursorToX(p_pib);
            break;
        } else {
            // Cross lines.
            width += line.cursorToX(lineEnd) - line.cursorToX(p_pib);

            // Move to next line.
            p_length = p_length - (lineEnd - p_pib);
            p_pib = lineEnd;
            line = layout->lineForTextPosition(p_pib + 1);
        }
    }

    return width > 0 ? width : -1;
}

bool VTable::isHeaderRow(int p_idx) const
{
    return p_idx == HeaderRowIndex;
}

bool VTable::isDelimiterRow(int p_idx) const
{
    return p_idx == DelimiterRowIndex;
}

QString VTable::generateFormattedText(const QString &p_core,
                                      int p_nrSpaces,
                                      Alignment p_align) const
{
    Q_ASSERT(p_align != Alignment::None);

    // Align left.
    int leftSpaces = 0;
    int rightSpaces = p_nrSpaces;

    if (p_align == Alignment::Center) {
        leftSpaces = p_nrSpaces / 2;
        rightSpaces = p_nrSpaces - leftSpaces;
    } else if (p_align == Alignment::Right) {
        leftSpaces = p_nrSpaces;
        rightSpaces = 0;
    }

    return QString("%1 %2%3%4 ").arg(c_borderChar,
                                     QString(leftSpaces, ' '),
                                     p_core,
                                     QString(rightSpaces, ' '));
}

VTable::Alignment VTable::getColumnAlignment(int p_idx) const
{
    Row *row = delimiter();
    if (row->m_cells.size() <= p_idx) {
        return Alignment::None;
    }

    QString core = row->m_cells[p_idx].m_text.mid(1).trimmed();
    Q_ASSERT(!core.isEmpty());
    bool leftColon = core[0] == ':';
    bool rightColon = core[core.size() - 1] == ':';
    if (leftColon) {
        if (rightColon) {
            return Alignment::Center;
        } else {
            return Alignment::Left;
        }
    } else {
        if (rightColon) {
            return Alignment::Right;
        } else {
            return Alignment::None;
        }
    }
}

static inline bool equalWidth(int p_a, int p_b, int p_margin = 5)
{
    return qAbs(p_a - p_b) < p_margin;
}

bool VTable::isDelimiterCellWellFormatted(const Cell &p_cell,
                                          const CellInfo &p_info,
                                          int p_targetWidth) const
{
    // We could use core width here for delimiter cell.
    if (!equalWidth(p_info.m_coreWidth, p_targetWidth, m_minusWidth)) {
        return false;
    }

    const QString &text = p_cell.m_text;
    if (text.size() < 4) {
        return false;
    }

    if (text[1] != ' ' || text[text.size() - 1] != ' ') {
        return false;
    }

    if (text[2] == ' ' || text[text.size() - 2] == ' ') {
        return false;
    }

    return true;
}

bool VTable::isCellWellFormatted(const Row &p_row,
                                 const Cell &p_cell,
                                 const CellInfo &p_info,
                                 int p_targetWidth,
                                 Alignment p_align) const
{
    Q_ASSERT(p_align != Alignment::None);

    const QString &text = p_cell.m_text;
    if (text.size() < 4) {
        return false;
    }

    if (text[1] != ' ' || text[text.size() - 1] != ' ') {
        return false;
    }

    // Skip alignment check of empty cell.
    if (p_info.m_coreOffset > 0) {
        int leftSpaces = p_info.m_coreOffset - 2;
        int rightSpaces = text.size() - p_info.m_coreOffset - p_info.m_coreLength - 1;
        switch (p_align) {
        case Alignment::Left:
            if (leftSpaces > 0) {
                return false;
            }

            break;

        case Alignment::Center:
            if (qAbs(leftSpaces - rightSpaces) > 1) {
                return false;
            }

            break;

        case Alignment::Right:
            if (rightSpaces > 0) {
                return false;
            }

            break;

        default:
            Q_ASSERT(false);
            break;
        }
    }

    // Calculate the width of the text without two spaces around.
    int cellWidth = calculateTextWidth(p_row.m_block,
                                       p_cell.m_offset + 2,
                                       p_cell.m_length - 3);
    if (!equalWidth(cellWidth, p_targetWidth, m_spaceWidth)) {
        return false;
    }

    return true;
}

void VTable::write()
{
    if (m_exist) {
        writeExist();
    } else {
        writeNonExist();
    }
}

void VTable::writeExist()
{
    bool changed = false;
    QTextCursor cursor = m_editor->textCursorW();
    int cursorBlock = -1, cursorPib = -1;

    // Write the table row by row.
    for (auto & row : m_rows) {
        bool needChange = false;
        for (const auto & cell : row.m_cells) {
            if (!cell.m_formattedText.isEmpty() || cell.m_deleted) {
                needChange = true;
                break;
            }
        }

        if (!needChange) {
            continue;
        }

        if (!changed) {
            changed = true;
            cursorBlock = cursor.blockNumber();
            cursorPib = cursor.positionInBlock();

            cursor.beginEditBlock();
        }

        // Construct the block text.
        QString newBlockText(row.m_preText);
        for (auto & cell : row.m_cells) {
            if (cell.m_deleted) {
                continue;
            }

            int pos = newBlockText.size();
            if (cell.m_formattedText.isEmpty()) {
                newBlockText += cell.m_text;
            } else {
                newBlockText += cell.m_formattedText;
            }

            if (cell.m_cursorCoreOffset > -1) {
                // Cursor in this cell.
                cursorPib = pos + cell.m_cursorCoreOffset + 2;
                if (cursorPib >= newBlockText.size()) {
                    cursorPib = newBlockText.size() - 1;
                }
            }
        }

        newBlockText += c_borderChar;

        // Replace the whole block.
        cursor.setPosition(row.m_block.position());
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cursor.insertText(newBlockText);
    }

    if (changed) {
        qDebug() << "write formatted table with cursor block" << cursorBlock;
        cursor.endEditBlock();
        m_editor->setTextCursorW(cursor);

        // Restore the cursor.
        QTextBlock block = m_editor->documentW()->findBlockByNumber(cursorBlock);
        if (block.isValid()) {
            int pos = block.position() + cursorPib;
            QTextCursor cur = m_editor->textCursorW();
            cur.setPosition(pos);
            m_editor->setTextCursorW(cur);
        }
    }
}

void VTable::writeNonExist()
{
    // Generate the text of the whole table.
    QString tableText;
    for (int rowIdx = 0; rowIdx < m_rows.size(); ++rowIdx) {
        const auto & row = m_rows[rowIdx];
        tableText += row.m_preText;
        for (auto & cell : row.m_cells) {
            if (cell.m_deleted) {
                continue;
            }

            tableText += cell.m_text;
        }

        tableText += c_borderChar;

        if (rowIdx < m_rows.size() - 1) {
            tableText += '\n';
        }
    }

    QTextCursor cursor = m_editor->textCursorW();
    int pos = cursor.position() + 2;
    cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::MoveAnchor);
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    cursor.insertText(tableText);
    cursor.setPosition(pos);
    m_editor->setTextCursorW(cursor);
}

void VTable::pruneColumns(int p_nrCols)
{
    for (auto & row : m_rows) {
        for (int i = p_nrCols; i < row.m_cells.size(); ++i) {
            row.m_cells[i].m_deleted = true;
        }
    }
}
