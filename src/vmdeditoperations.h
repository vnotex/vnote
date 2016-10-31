#ifndef VMDEDITOPERATIONS_H
#define VMDEDITOPERATIONS_H

#include <QObject>
#include "veditoperations.h"

// Editor operations for Markdown
class VMdEditOperations : public VEditOperations
{
public:
    VMdEditOperations(VEdit *editor, VNoteFile *noteFile);
    bool insertImageFromMimeData(const QMimeData *source) Q_DECL_OVERRIDE;
    bool insertURLFromMimeData(const QMimeData *source) Q_DECL_OVERRIDE;
    bool insertImageFromPath(const QString &imagePath);
};

#endif // VMDEDITOPERATIONS_H
