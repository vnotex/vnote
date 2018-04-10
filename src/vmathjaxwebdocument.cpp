#include "vmathjaxwebdocument.h"

#include <QDebug>

VMathJaxWebDocument::VMathJaxWebDocument(QObject *p_parent)
    : QObject(p_parent)
{
}

void VMathJaxWebDocument::previewMathJax(int p_identifier,
                                         int p_id,
                                         const QString &p_text)
{
    emit requestPreviewMathJax(p_identifier, p_id, p_text);
}

void VMathJaxWebDocument::mathjaxResultReady(int p_identifier,
                                             int p_id,
                                             const QString &p_format,
                                             const QString &p_data)
{
    emit mathjaxPreviewResultReady(p_identifier, p_id, p_format, p_data);
}
