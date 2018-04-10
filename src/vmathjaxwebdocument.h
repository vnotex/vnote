#ifndef VMATHJAXWEBDOCUMENT_H
#define VMATHJAXWEBDOCUMENT_H

#include <QObject>

class VMathJaxWebDocument : public QObject
{
    Q_OBJECT
public:
    explicit VMathJaxWebDocument(QObject *p_parent = nullptr);

    void previewMathJax(int p_identifier, int p_id, const QString &p_text);

public slots:
    // Will be called in the HTML side

    void mathjaxResultReady(int p_identifier,
                            int p_id,
                            const QString &p_format,
                            const QString &p_data);

signals:
    void requestPreviewMathJax(int p_identifier, int p_id, const QString &p_text);

    void mathjaxPreviewResultReady(int p_identifier,
                                   int p_id,
                                   const QString &p_format,
                                   const QString &p_data);
};

#endif // VMATHJAXWEBDOCUMENT_H
