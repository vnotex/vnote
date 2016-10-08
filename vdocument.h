#ifndef VDOCUMENT_H
#define VDOCUMENT_H

#include <QObject>
#include <QString>

class VDocument : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString text MEMBER m_text NOTIFY textChanged)
public:
    explicit VDocument(QObject *parent = 0);
    VDocument(const QString &text, QObject *parent = 0);
    void setText(const QString &text);
    QString getText();

signals:
    void textChanged(const QString &text);

private:
    QString m_text;
};

#endif // VDOCUMENT_H
