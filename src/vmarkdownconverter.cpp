#include "vmarkdownconverter.h"

VMarkdownConverter::VMarkdownConverter()   
{
    hoedownHtmlFlags = (hoedown_html_flags)0;
    nestingLevel = 16;

    htmlRenderer = hoedown_html_renderer_new(hoedownHtmlFlags, nestingLevel);
    tocRenderer = hoedown_html_toc_renderer_new(nestingLevel);
}

VMarkdownConverter::~VMarkdownConverter()
{
    if (htmlRenderer) {
        hoedown_html_renderer_free(htmlRenderer);
    }
    if (tocRenderer) {
        hoedown_html_renderer_free(tocRenderer);
    }
}

QString VMarkdownConverter::generateHtml(const QString &markdown, hoedown_extensions options)
{
    if (markdown.isEmpty()) {
        return QString();
    }
    hoedown_document *document = hoedown_document_new(htmlRenderer, options,
                                                      nestingLevel);
    QByteArray data = markdown.toUtf8();
    hoedown_buffer *outBuf = hoedown_buffer_new(data.size());
    hoedown_document_render(document, outBuf, (const uint8_t *)data.constData(), data.size());
    hoedown_document_free(document);
    QString html = QString::fromUtf8(hoedown_buffer_cstr(outBuf));
    hoedown_buffer_free(outBuf);

    return html;
}

QString VMarkdownConverter::generateToc(const QString &markdown, hoedown_extensions options)
{
    if (markdown.isEmpty()) {
        return QString();
    }
    hoedown_document *document = hoedown_document_new(tocRenderer, options, nestingLevel);
    QByteArray data = markdown.toUtf8();
    hoedown_buffer *outBuf = hoedown_buffer_new(16);
    hoedown_document_render(document, outBuf, (const uint8_t *)data.constData(), data.size());
    hoedown_document_free(document);
    QString toc = QString::fromUtf8(hoedown_buffer_cstr(outBuf));
    hoedown_buffer_free(outBuf);
    return toc;
}
