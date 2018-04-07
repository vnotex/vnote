#include "vlinenumberarea.h"

#include <QPaintEvent>
#include <QTextDocument>

VLineNumberArea::VLineNumberArea(VTextEditWithLineNumber *p_editor,
                                 const QTextDocument *p_document,
                                 int p_digitWidth,
                                 QWidget *p_parent)
    : QWidget(p_parent),
      m_editor(p_editor),
      m_document(p_document),
      m_width(0),
      m_blockCount(-1),
      m_digitWidth(p_digitWidth),
      m_foregroundColor("black"),
      m_backgroundColor("grey")
{
}

int VLineNumberArea::calculateWidth() const
{
    int bc = m_document->blockCount();
    if (m_blockCount == bc) {
        return m_width;
    }

    const_cast<VLineNumberArea *>(this)->m_blockCount = bc;
    int digits = 1;
    int max = qMax(1, m_blockCount);
    while (max >= 10) {
        max /= 10;
        ++digits;
    }

    const_cast<VLineNumberArea *>(this)->m_width = m_digitWidth * digits + 3;

    return m_width;
}
