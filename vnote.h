#ifndef VNOTE_H
#define VNOTE_H

#include <QString>
#include <QVector>
#include <QSettings>
#include <QFont>
#include "vnotebook.h"

class VNote
{
public:
    VNote();

    const QVector<VNotebook>& getNotebooks();

    static void decorateTemplate();

    static QString templateHtml;

private:
    QVector<VNotebook> notebooks;
};

#endif // VNOTE_H
