#ifndef VMDEDITOPERATIONS_H
#define VMDEDITOPERATIONS_H

#include <QObject>
#include <QString>
#include <QUrl>
#include <QImage>
#include "veditoperations.h"

// Editor operations for Markdown
class VMdEditOperations : public VEditOperations
{
public:
    VMdEditOperations(VEdit *p_editor, VFile *p_file);
    bool insertImageFromMimeData(const QMimeData *source) Q_DECL_OVERRIDE;
    bool insertURLFromMimeData(const QMimeData *source) Q_DECL_OVERRIDE;
    bool insertImageFromURL(const QUrl &imageUrl);
    void insertImageFromPath(const QString &title, const QString &path, const QString &oriImagePath);
    void insertImageFromQImage(const QString &title, const QString &path, const QImage &image);
};

#endif // VMDEDITOPERATIONS_H
