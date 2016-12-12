#include <QtWidgets>
#include <QVector>
#include "vedit.h"
#include "vnote.h"
#include "vconfigmanager.h"
#include "vtoc.h"
#include "utils/vutils.h"
#include "veditoperations.h"

extern VConfigManager vconfig;

VEdit::VEdit(VFile *p_file, QWidget *p_parent)
    : QTextEdit(p_parent), m_file(p_file), m_editOps(NULL)
{
    connect(document(), &QTextDocument::modificationChanged,
            (VFile *)m_file, &VFile::setModified);
}

VEdit::~VEdit()
{
    qDebug() << "VEdit destruction";
    if (m_file) {
        disconnect(document(), &QTextDocument::modificationChanged,
                   (VFile *)m_file, &VFile::setModified);
    }
    if (m_editOps) {
        delete m_editOps;
        m_editOps = NULL;
    }
}

void VEdit::beginEdit()
{
    setReadOnly(false);
    setModified(false);
}

void VEdit::endEdit()
{
    setReadOnly(true);
}

void VEdit::saveFile()
{
    if (!document()->isModified()) {
        return;
    }
    m_file->setContent(toHtml());
    document()->setModified(false);
}

void VEdit::reloadFile()
{
    setHtml(m_file->getContent());
    setModified(false);
}

void VEdit::scrollToLine(int p_lineNumber)
{
    Q_ASSERT(p_lineNumber >= 0);

    // Move the cursor to the end first
    moveCursor(QTextCursor::End);
    QTextCursor cursor(document()->findBlockByLineNumber(p_lineNumber));
    cursor.movePosition(QTextCursor::EndOfBlock);
    setTextCursor(cursor);

    setFocus();
}

bool VEdit::isModified() const
{
    return document()->isModified();
}

void VEdit::setModified(bool p_modified)
{
    document()->setModified(p_modified);
    if (m_file) {
        m_file->setModified(p_modified);
    }
}

void VEdit::insertImage()
{
    if (m_editOps) {
        m_editOps->insertImage();
    }
}
