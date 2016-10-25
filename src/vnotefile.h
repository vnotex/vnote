#ifndef VNOTEFILE_H
#define VNOTEFILE_H

#include <QString>
#include "vconstants.h"

class VNoteFile
{
public:
    VNoteFile(const QString &basePath, const QString &fileName, const QString &content,
              DocType docType, bool modifiable);

    QString basePath;
    QString fileName;
    QString content;
    DocType docType;
    bool modifiable;
};

#endif // VNOTEFILE_H
