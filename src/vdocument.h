#ifndef VDOCUMENT_H
#define VDOCUMENT_H

#include <QObject>
#include <QString>

class VDocument : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString text MEMBER m_text NOTIFY textChanged)
    Q_PROPERTY(QString toc MEMBER m_toc NOTIFY tocChanged)
    Q_PROPERTY(QString html MEMBER m_html NOTIFY htmlChanged)

public:
    explicit VDocument(QObject *parent = 0);
    VDocument(const QString &text, QObject *parent = 0);
    void setText(const QString &text);
    QString getText();
    QString getToc();
    void scrollToAnchor(const QString &anchor);
    void setHtml(const QString &html);

public slots:
    // Will be called in the HTML side
    void setToc(const QString &toc);
    void setHeader(const QString &anchor);

signals:
    void textChanged(const QString &text);
    void tocChanged(const QString &toc);
    void requestScrollToAnchor(const QString &anchor);
    void headerChanged(const QString &anchor);
    void htmlChanged(const QString &html);

private:
    QString m_text;
    QString m_toc;
    QString m_header;
    QString m_html;
};

#endif // VDOCUMENT_H
