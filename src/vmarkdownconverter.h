#ifndef VMARKDOWNCONVERTER_H
#define VMARKDOWNCONVERTER_H

#include <QString>

extern "C" {
#include <src/html.h>
#include <src/document.h>
}

class VMarkdownConverter
{
public:
    VMarkdownConverter();
    ~VMarkdownConverter();

    QString generateHtml(const QString &markdown, hoedown_extensions options);
    QString generateToc(const QString &markdown, hoedown_extensions options);

private:
    // VMarkdownDocument *generateDocument(const QString &markdown);
    hoedown_html_flags hoedownHtmlFlags;
    int nestingLevel;
    hoedown_renderer *htmlRenderer;
    hoedown_renderer *tocRenderer;
};

#endif // VMARKDOWNCONVERTER_H
