#ifndef VEDIT_H
#define VEDIT_H

#include <QTextEdit>
#include <QString>

class VEdit : public QTextEdit
{
    Q_OBJECT
public:
    explicit VEdit(const QString &path, const QString &name,
                   QWidget *parent = 0);
    bool requestClose();

signals:

public slots:


private:
    QString readFile(const QString &filePath);
    bool writeFile(const QString &filePath, const QString &text);
    void showTextReadMode();

    QString path;
    QString name;
    QString fileText;
};

#endif // VEDIT_H
