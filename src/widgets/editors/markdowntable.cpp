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

qreal MarkdownTable::s_spaceWidth = -1;

qreal MarkdownTable::s_minusWidth = -1;

qreal MarkdownTable::s_colonWidth = -1;

qreal MarkdownTable::s_defaultDelimiterWidth = -1;

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
    Q_ASSERT(p_bodyRow >= 0 && p_col > 0);
    m_rows.resize(p_bodyRow + 2);

    // PreText for each row.
    QString preText;
    const QTextCursor cursor = m_textEdit->textCursor();
    Q_ASSERT(cursor.atBlockEnd());
    if (!cursor.atBlockStart()) {
        preText = cursor.block().text();
    }

    QString delimiterCore(c_defaultDelimiter);
    switch (p_alignment) {
    case Alignment::Left:
        delimiterCore[0] = ':';
        break;

    case Alignment::Center:
        delimiterCore[0] = ':';
        delimiterCore[delimiterCore.size() - 1] = ':';
        break;

    case Alignment::Right:
        delimiterCore[delimiterCore.size() - 1] = ':';
        break;

    default:
        break;
    }
    const QString delimiterCell = generateFormattedText(delimiterCore, 0);
    const QString contentCell = generateFormattedText(QString(c_defaultDelimiter.size(), QLatin1Char(' ')), 0);

    for (int rowIdx = 0; rowIdx < m_rows.size(); ++rowIdx) {
        auto &row = m_rows[rowIdx];
        row.m_preText = preText;
        row.m_cells.resize(p_col);

        const QString &content = isDelimiterRow(rowIdx) ? delimiterCell : contentCell;
        for (auto &cell : row.m_cells) {
            cell.m_text = content;
        }
    }
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

    const QTextCursor cursor = m_textEdit->textCursor();
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
    if (m_isNew) {
        writeNewTable();
    } else {
        writeTable();
    }
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

    QFontMetricsF fmf(font);
    s_spaceWidth = fmf.width(' ');
    s_minusWidth = fmf.width('-');
    s_colonWidth = fmf.width(':');
    s_defaultDelimiterWidth = fmf.width(c_defaultDelimiter);

    qDebug() << "smart table widths" << font.family() << s_spaceWidth << s_minusWidth << s_colonWidth << s_defaultDelimiterWidth;
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
    // Target width of this column.
    qreal targetWidth = 0;
    fetchCellInfoOfColumn(p_idx, p_cursorRowIdx, p_cursorPib, cells, targetWidth);

    // Get the alignment of this column.
    const auto align = getColumnAlignment(p_idx);

    // Calculate the formatted text of each cell.
    for (int rowIdx = 0; rowIdx < cells.size(); ++rowIdx) {
        const auto &info = cells[rowIdx];
        auto &row = m_rows[rowIdx];
        if (row.m_cells.size() <= p_idx) {
            row.m_cells.resize(p_idx + 1);
        }
        auto &cell = row.m_cells[p_idx];
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
                int delta = s_minusWidth - 1;
                switch (align) {
                case Alignment::None:
                {
                    int coreLength = static_cast<int>((targetWidth + delta) / s_minusWidth);
                    core = QString(coreLength, '-');
                    break;
                }

                case Alignment::Left:
                {
                    int coreLength = static_cast<int>((targetWidth - s_colonWidth + delta) / s_minusWidth);
                    core = QStringLiteral(":");
                    core += QString(coreLength, '-');
                    break;
                }

                case Alignment::Center:
                {
                    int coreLength = static_cast<int>((targetWidth - 2 * s_colonWidth + delta) / s_minusWidth);
                    core = QStringLiteral(":");
                    core += QString(coreLength, '-');
                    core += QStringLiteral(":");
                    break;
                }

                case Alignment::Right:
                {
                    int coreLength = static_cast<int>((targetWidth - s_colonWidth + delta) / s_minusWidth);
                    core = QString(coreLength, '-');
                    core += QStringLiteral(":");
                    break;
                }

                default:
                    Q_ASSERT(false);
                    break;
                }

                cell.m_formattedText = generateFormattedText(core, 0);
                if (cell.m_text == cell.m_formattedText) {
                    // Avoid infinite change.
                    cell.m_formattedText.clear();
                }
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
                int nr = static_cast<int>((targetWidth - info.m_coreWidth + s_spaceWidth - 1) / s_spaceWidth);
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
                                          int p_cursorRowIdx,
                                          int p_cursorPib,
                                          QVector<CellInfo> &p_cellsInfo,
                                          qreal &p_targetWidth) const
{
    p_targetWidth = s_defaultDelimiterWidth;
    p_cellsInfo.resize(m_rows.size());

    // Fetch the trimmed core content and its width.
    for (int i = 0; i < m_rows.size(); ++i) {
        const auto &row = m_rows[i];
        auto &info = p_cellsInfo[i];

        if (row.m_cells.size() <= p_idx) {
            // Need to add a new cell later.
            continue;
        }

        // Get the info of this cell.
        const auto &cell = row.m_cells[p_idx];
        int first = fetchCoreOffset(cell.m_text);
        if (first == -1) {
            // Empty cell.
            continue;
        }
        info.m_coreOffset = first;

        // If the cursor is in this cell, then we should treat the core length at least not
        // less than the cursor position even if there is trailing spaces before the cursor.
        int last = cell.m_length - 1;
        for (; last >= first; --last) {
            if ((p_cursorRowIdx == i && p_cursorPib - cell.m_offset - 1 == last) || cell.m_text[last] != ' ') {
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

bool MarkdownTable::isDelimiterRow(int p_idx) const
{
    return p_idx == DelimiterRowIndex;
}

qreal MarkdownTable::calculateTextWidth(const QTextBlock &p_block, int p_pib, int p_length) const
{
    // The block may cross multiple lines.
    qreal textWidth = 0;
    QTextLayout *layout = p_block.layout();
    QTextLine line = layout->lineForTextPosition(p_pib);
    while (line.isValid()) {
        int lineEnd = line.textStart() + line.textLength();
        if (lineEnd >= p_pib + p_length) {
            // The last line.
            textWidth += line.cursorToX(p_pib + p_length) - line.cursorToX(p_pib);
            break;
        } else {
            // Cross lines.
            textWidth += line.cursorToX(lineEnd) - line.cursorToX(p_pib);

            // Move to next line.
            p_length = p_length - (lineEnd - p_pib);
            p_pib = lineEnd;
            line = layout->lineForTextPosition(p_pib + 1);
        }
    }

    return textWidth > 0 ? textWidth : -1;
}

Alignment MarkdownTable::getColumnAlignment(int p_idx) const
{
    auto row = delimiter();
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

static bool equalWidth(int p_a, int p_b, int p_margin = 5)
{
    return qAbs(p_a - p_b) <= p_margin;
}

bool MarkdownTable::isDelimiterCellWellFormatted(const Cell &p_cell,
                                                 const CellInfo &p_info,
                                                 qreal p_targetWidth) const
{
    // We could use core width here for delimiter cell.
    if (!equalWidth(p_info.m_coreWidth, p_targetWidth, s_minusWidth / 2)) {
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

QString MarkdownTable::generateFormattedText(const QString &p_core,
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

bool MarkdownTable::isHeaderRow(int p_idx) const
{
    return p_idx == HeaderRowIndex;
}

bool MarkdownTable::isCellWellFormatted(const Row &p_row,
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
    if (!equalWidth(cellWidth, p_targetWidth, s_spaceWidth / 2)) {
        return false;
    }

    return true;
}

void MarkdownTable::writeTable()
{
    bool changed = false;
    // Use cursor(QTextDocument) to handle the corner case when cursor locates at the end of one row.
    QTextCursor cursor(m_textEdit->document());
    int cursorBlock = -1, cursorPib = -1;
    bool cursorHit = false;

    // Write the table row by row.
    for (const auto &row : m_rows) {
        bool needChange = false;
        for (const auto &cell : row.m_cells) {
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
            const QTextCursor curCursor = m_textEdit->textCursor();
            cursorBlock = curCursor.blockNumber();
            cursorPib = curCursor.positionInBlock();
            cursor.beginEditBlock();
        }

        // Construct the block text.
        QString newBlockText(row.m_preText);
        for (const auto &cell : row.m_cells) {
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
                cursorHit = true;
                // We need to calculate the new core offset of this cell.
                // For delimiter row, this way won't work, but that is fine.
                int coreOffset = fetchCoreOffset(cell.m_formattedText.isEmpty() ? cell.m_text : cell.m_formattedText);
                cursorPib = pos + cell.m_cursorCoreOffset + coreOffset;
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

        // Restore the cursor.
        if (cursorHit) {
            QTextBlock block = m_textEdit->document()->findBlockByNumber(cursorBlock);
            if (block.isValid()) {
                int pos = block.position() + cursorPib;
                auto curCursor = m_textEdit->textCursor();
                curCursor.setPosition(pos);
                m_textEdit->setTextCursor(curCursor);
            }
        }
    }
}

void MarkdownTable::writeNewTable()
{
    // Generate the text of the whole table.
    QString tableText;
    for (int rowIdx = 0; rowIdx < m_rows.size(); ++rowIdx) {
        const auto &row = m_rows[rowIdx];
        tableText += row.m_preText;
        for (const auto &cell : row.m_cells) {
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

    QTextCursor cursor = m_textEdit->textCursor();
    int pos = cursor.position() + 2;
    cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::MoveAnchor);
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    cursor.insertText(tableText);
    cursor.setPosition(pos);
    m_textEdit->setTextCursor(cursor);
}

int MarkdownTable::fetchCoreOffset(const QString &p_cellText)
{
    // [0] is the border char. To find the offset of the core content.
    for (int i = 1; i < p_cellText.size(); ++i) {
        if (p_cellText[i] != ' ') {
            return i;
        }
    }

    return -1;
}
