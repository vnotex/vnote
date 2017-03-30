#ifndef VDOCUMENT_H
#define VDOCUMENT_H

#include <QObject>
#include <QString>

class VFile;

class VDocument : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString text MEMBER m_text NOTIFY textChanged)
    Q_PROPERTY(QString toc MEMBER m_toc NOTIFY tocChanged)
    Q_PROPERTY(QString html MEMBER m_html NOTIFY htmlChanged)

public:
    VDocument(const VFile *p_file, QObject *p_parent = 0);
    QString getToc();
    void scrollToAnchor(const QString &anchor);
    void setHtml(const QString &html);

public slots:
    // Will be called in the HTML side
    void setToc(const QString &toc);
    void setHeader(const QString &anchor);
    void setLog(const QString &p_log);
    void keyPressEvent(int p_key, bool p_ctrl, bool p_shift);
    void updateText();

signals:
    void textChanged(const QString &text);
    void tocChanged(const QString &toc);
    void requestScrollToAnchor(const QString &anchor);
    void headerChanged(const QString &anchor);
    void htmlChanged(const QString &html);
    void logChanged(const QString &p_log);
    void keyPressed(int p_key, bool p_ctrl, bool p_shift);

private:
    QString m_toc;
    QString m_header;

    // m_text does NOT contain actual content.
    QString m_text;

    // When using Hoedown, m_html will contain the html content.
    QString m_html;

    const VFile *m_file;
};

#endif // VDOCUMENT_H
