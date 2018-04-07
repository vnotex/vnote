#ifndef VDOCUMENT_H
#define VDOCUMENT_H

#include <QObject>
#include <QString>

#include "vwordcountinfo.h"

class VFile;
class VPlantUMLHelper;
class VGraphvizHelper;

class VDocument : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString text MEMBER m_text NOTIFY textChanged)
    Q_PROPERTY(QString toc MEMBER m_toc NOTIFY tocChanged)
    Q_PROPERTY(QString html MEMBER m_html NOTIFY htmlChanged)

public:
    // @p_file could be NULL.
    VDocument(const VFile *p_file, QObject *p_parent = 0);

    QString getToc();

    // Scroll to @anchor in the web.
    // @anchor is the id without '#', like "toc_1". If empty, will scroll to top.
    void scrollToAnchor(const QString &anchor);

    void setHtml(const QString &html);

    // Request to highlight a segment text.
    // Use p_id to identify the result.
    void highlightTextAsync(const QString &p_text, int p_id, int p_timeStamp);

    // Request to convert @p_text to HTML.
    void textToHtmlAsync(const QString &p_text);

    void setFile(const VFile *p_file);

    bool isReadyToHighlight() const;

    bool isReadyToTextToHtml() const;

    // Request to get the HTML content.
    void getHtmlContentAsync();

    const VWordCountInfo &getWordCountInfo() const;

    // Whether change to preview mode.
    void setPreviewEnabled(bool p_enabled);

    // @p_livePreview: if true, display the result in the preview-div; otherwise,
    // call previewCodeBlockCB() to pass back the result.
    // Only for online parser.
    void previewCodeBlock(int p_id,
                          const QString &p_lang,
                          const QString &p_text,
                          bool p_livePreview);

    // Set the content of the preview.
    void setPreviewContent(const QString &p_lang, const QString &p_html);

public slots:
    // Will be called in the HTML side

    // @toc: the HTML of the TOC.
    // @baseLevel: the base level of @toc, starting from 1. It is the top level
    // in the @toc.
    void setToc(const QString &toc, int baseLevel);

    // When the Web view has been scrolled, it will signal current header anchor.
    // Empty @anchor to indicate an invalid header.
    // The header does not begins with '#'.
    void setHeader(const QString &anchor);

    void setLog(const QString &p_log);
    void keyPressEvent(int p_key, bool p_ctrl, bool p_shift);
    void updateText();

    void highlightTextCB(const QString &p_html, int p_id, int p_timeStamp);

    void noticeReadyToHighlightText();

    void textToHtmlCB(const QString &p_text, const QString &p_html);

    void noticeReadyToTextToHtml();

    // Web-side handle logics (MathJax etc.) is finished.
    // But the page may not finish loading, such as images.
    void finishLogics();

    void htmlContentCB(const QString &p_head,
                       const QString &p_style,
                       const QString &p_body);

    void updateWordCountInfo(int p_wordCount,
                             int p_charWithoutSpacesCount,
                             int p_charWithSpacesCount);

    // Web-side call this to process PlantUML locally.
    void processPlantUML(int p_id, const QString &p_format, const QString &p_text);

    // Web-side call this to process Graphviz locally.
    void processGraphviz(int p_id, const QString &p_format, const QString &p_text);

signals:
    void textChanged(const QString &text);

    void tocChanged(const QString &toc);

    void requestScrollToAnchor(const QString &anchor);

    // @anchor is the id of that anchor, without '#'.
    void headerChanged(const QString &anchor);

    void htmlChanged(const QString &html);

    void logChanged(const QString &p_log);

    void keyPressed(int p_key, bool p_ctrl, bool p_shift);

    void requestHighlightText(const QString &p_text, int p_id, int p_timeStamp);

    void textHighlighted(const QString &p_html, int p_id, int p_timeStamp);

    void readyToHighlightText();

    void logicsFinished();

    void requestTextToHtml(const QString &p_text);

    void textToHtmlFinished(const QString &p_text, const QString &p_html);

    void requestHtmlContent();

    void htmlContentFinished(const QString &p_headContent,
                             const QString &p_styleContent,
                             const QString &p_bodyContent);

    void wordCountInfoUpdated();

    void plantUMLResultReady(int p_id, const QString &p_format, const QString &p_result);

    void graphvizResultReady(int p_id, const QString &p_format, const QString &p_result);

    void requestPreviewEnabled(bool p_enabled);

    void requestPreviewCodeBlock(int p_id,
                                 const QString &p_lang,
                                 const QString &p_text,
                                 bool p_livePreview);

    void requestSetPreviewContent(const QString &p_lang, const QString &p_html);

private:
    QString m_toc;
    QString m_header;

    // m_text does NOT contain actual content.
    QString m_text;

    // When using Hoedown, m_html will contain the html content.
    QString m_html;

    const VFile *m_file;

    // Whether the web side is ready to handle highlight text request.
    bool m_readyToHighlight;

    // Whether the web side is ready to convert text to html.
    bool m_readyToTextToHtml;

    VWordCountInfo m_wordCountInfo;

    VPlantUMLHelper *m_plantUMLHelper;

    VGraphvizHelper *m_graphvizHelper;
};

inline bool VDocument::isReadyToHighlight() const
{
    return m_readyToHighlight;
}

inline bool VDocument::isReadyToTextToHtml() const
{
    return m_readyToTextToHtml;
}

inline const VWordCountInfo &VDocument::getWordCountInfo() const
{
    return m_wordCountInfo;
}

#endif // VDOCUMENT_H
