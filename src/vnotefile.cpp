#include "vnotefile.h"

VNoteFile::VNoteFile(const QString &basePath, const QString &fileName,
                     const QString &content, DocType docType, bool modifiable)
    : basePath(basePath), fileName(fileName),
      content(content), docType(docType), modifiable(modifiable)
{

}
