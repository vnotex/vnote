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
    bool insertImage() Q_DECL_OVERRIDE;
    bool handleKeyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;

private:
    bool insertImageFromURL(const QUrl &imageUrl);
    void insertImageFromPath(const QString &title, const QString &path, const QString &oriImagePath);
    void insertImageFromQImage(const QString &title, const QString &path, const QImage &image);

    // Key press handlers.
    bool handleKeyTab(QKeyEvent *p_event);
    bool handleKeyBackTab(QKeyEvent *p_event);
    bool handleKeyB(QKeyEvent *p_event);
    bool handleKeyH(QKeyEvent *p_event);
    bool handleKeyI(QKeyEvent *p_event);
    bool handleKeyU(QKeyEvent *p_event);
    bool handleKeyW(QKeyEvent *p_event);
};

#endif // VMDEDITOPERATIONS_H
