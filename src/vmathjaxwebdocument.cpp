#include "vmathjaxwebdocument.h"

VMathJaxWebDocument::VMathJaxWebDocument(QObject *p_parent)
    : QObject(p_parent)
{
}

void VMathJaxWebDocument::previewMathJax(int p_identifier,
                                         int p_id,
                                         TimeStamp p_timeStamp,
                                         const QString &p_text,
                                         bool p_isHtml)
{
    emit requestPreviewMathJax(p_identifier, p_id, p_timeStamp, p_text, p_isHtml);
}

void VMathJaxWebDocument::mathjaxResultReady(int p_identifier,
                                             int p_id,
                                             unsigned long long p_timeStamp,
                                             const QString &p_format,
                                             const QString &p_data)
{
    emit mathjaxPreviewResultReady(p_identifier, p_id, p_timeStamp, p_format, p_data);
}

void VMathJaxWebDocument::diagramResultReady(int p_identifier,
                                             int p_id,
                                             unsigned long long p_timeStamp,
                                             const QString &p_format,
                                             const QString &p_data)
{
    emit diagramPreviewResultReady(p_identifier, p_id, p_timeStamp, p_format, p_data);
}

void VMathJaxWebDocument::previewDiagram(int p_identifier,
                                         int p_id,
                                         TimeStamp p_timeStamp,
                                         const QString &p_lang,
                                         const QString &p_text)
{
    emit requestPreviewDiagram(p_identifier,
                               p_id,
                               p_timeStamp,
                               p_lang,
                               p_text);
}
