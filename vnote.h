#ifndef VNOTE_H
#define VNOTE_H

#include <QString>
#include <QVector>
#include <QSettings>
#include <QFont>
#include <QObject>
#include "vnotebook.h"

class VNote : public QObject
{
    Q_OBJECT
public:
    VNote();

    const QVector<VNotebook>& getNotebooks();

    static void decorateTemplate();

    static QString templateHtml;

    void createNotebook(const QString &name, const QString &path);

signals:
    void notebooksChanged(const QVector<VNotebook> &notebooks);

private:
    QVector<VNotebook> notebooks;
};

#endif // VNOTE_H
