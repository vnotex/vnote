#ifndef VMATHJAXWEBDOCUMENT_H
#define VMATHJAXWEBDOCUMENT_H

#include <QObject>

#include "vconstants.h"

class VMathJaxWebDocument : public QObject
{
    Q_OBJECT
public:
    explicit VMathJaxWebDocument(QObject *p_parent = nullptr);

    void previewMathJax(int p_identifier,
                        int p_id,
                        TimeStamp p_timeStamp,
                        const QString &p_text,
                        bool p_isHtml);

    void previewDiagram(int p_identifier,
                        int p_id,
                        TimeStamp p_timeStamp,
                        const QString &p_lang,
                        const QString &p_text);

public slots:
    // Will be called in the HTML side

    void mathjaxResultReady(int p_identifier,
                            int p_id,
                            unsigned long long p_timeStamp,
                            const QString &p_format,
                            const QString &p_data);

    void diagramResultReady(int p_identifier,
                            int p_id,
                            unsigned long long p_timeStamp,
                            const QString &p_format,
                            const QString &p_data);

signals:
    void requestPreviewMathJax(int p_identifier,
                               int p_id,
                               unsigned long long p_timeStamp,
                               const QString &p_text,
                               bool p_isHtml);

    void requestPreviewDiagram(int p_identifier,
                               int p_id,
                               unsigned long long p_timeStamp,
                               const QString &p_lang,
                               const QString &p_text);

    void mathjaxPreviewResultReady(int p_identifier,
                                   int p_id,
                                   TimeStamp p_timeStamp,
                                   const QString &p_format,
                                   const QString &p_data);

    void diagramPreviewResultReady(int p_identifier,
                                   int p_id,
                                   TimeStamp p_timeStamp,
                                   const QString &p_format,
                                   const QString &p_data);
};

#endif // VMATHJAXWEBDOCUMENT_H
