#include "vnotefile.h"

VNoteFile::VNoteFile(const QString &path, const QString &name,
                     const QString &content, DocType docType, bool modifiable)
    : path(path), name(name), content(content), docType(docType),
      modifiable(modifiable)
{

}
