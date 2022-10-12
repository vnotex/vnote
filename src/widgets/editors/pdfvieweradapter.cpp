#include "pdfvieweradapter.h"

using namespace vnotex;

PdfViewerAdapter::PdfViewerAdapter(QObject *p_parent)
    : QObject(p_parent)
{
}

PdfViewerAdapter::~PdfViewerAdapter()
{
}

void PdfViewerAdapter::setText(int p_revision,
                                    const QString &p_text,
                                    int p_lineNumber)
{
    emit textUpdated(p_text);
}

void PdfViewerAdapter::setText(const QString &p_text, int p_lineNumber)
{
    setText(0, p_text, p_lineNumber);
}
