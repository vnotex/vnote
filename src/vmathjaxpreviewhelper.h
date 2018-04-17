#ifndef VMATHJAXPREVIEWHELPER_H
#define VMATHJAXPREVIEWHELPER_H

#include <QObject>
#include <functional>
#include <QVector>

#include "vconstants.h"

class QWebEngineView;
class VMathJaxWebDocument;
class QWidget;

typedef std::function<void(void)> PendingFunc;

class VMathJaxPreviewHelper : public QObject
{
    Q_OBJECT
public:
    explicit VMathJaxPreviewHelper(QWidget *p_parentWidget, QObject *p_parent = nullptr);

    ~VMathJaxPreviewHelper();

    // Get an ID for identification.
    int registerIdentifier();

    // Preview @p_text and return PNG data asynchronously.
    // @p_identifier: identifier the caller registered;
    // @p_id: internal id for each caller;
    // @p_text: raw text of the MathJax script.
    void previewMathJax(int p_identifier, int p_id, TimeStamp p_timeStamp, const QString &p_text);

    void previewMathJaxFromHtml(int p_identitifer, int p_id, TimeStamp p_timeStamp, const QString &p_html);

    // Preview @p_text and return PNG data asynchronously.
    // @p_identifier: identifier the caller registered;
    // @p_id: internal id for each caller;
    // @p_lang: language of the diagram;
    // @p_text: raw text of the script.
    void previewDiagram(int p_identifier,
                        int p_id,
                        TimeStamp p_timeStamp,
                        const QString &p_lang,
                        const QString &p_text);

signals:
    void mathjaxPreviewResultReady(int p_identifier,
                                   int p_id,
                                   TimeStamp p_timeStamp,
                                   const QString &p_format,
                                   const QByteArray &p_data);

    void diagramPreviewResultReady(int p_identifier,
                                   int p_id,
                                   TimeStamp p_timeStamp,
                                   const QString &p_format,
                                   const QByteArray &p_data);

private:
    void init();

    void doInit();

    QWidget *m_parentWidget;

    int m_initialized;

    int m_nextID;

    QWebEngineView *m_webView;

    VMathJaxWebDocument *m_webDoc;

    bool m_webReady;

    QVector<PendingFunc> m_pendingFunc;
};

inline int VMathJaxPreviewHelper::registerIdentifier()
{
    return m_nextID++;
}

inline void VMathJaxPreviewHelper::init()
{
    if (!m_initialized) {
        doInit();
    }
}
#endif // VMATHJAXPREVIEWHELPER_H
