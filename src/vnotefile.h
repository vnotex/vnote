#ifndef VNOTEFILE_H
#define VNOTEFILE_H

#include <QString>
#include "vconstants.h"

class VNoteFile
{
public:
    VNoteFile(const QString &path, const QString &name, const QString &content,
              DocType docType, bool modifiable);

    QString path;
    QString name;
    QString content;
    DocType docType;
    bool modifiable;
};

#endif // VNOTEFILE_H
