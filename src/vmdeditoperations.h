#ifndef VMDEDITOPERATIONS_H
#define VMDEDITOPERATIONS_H

#include <QObject>
#include <QString>
#include <QUrl>
#include <QImage>
#include <QTextBlock>
#include "veditoperations.h"

class QTimer;

// Editor operations for Markdown
class VMdEditOperations : public VEditOperations
{
    Q_OBJECT
public:
    VMdEditOperations(VEdit *p_editor, VFile *p_file);
    bool insertImageFromMimeData(const QMimeData *source) Q_DECL_OVERRIDE;
    bool insertImage() Q_DECL_OVERRIDE;
    bool handleKeyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;
    bool insertImageFromURL(const QUrl &p_imageUrl) Q_DECL_OVERRIDE;

private:
    void insertImageFromPath(const QString &title, const QString &path, const QString &oriImagePath);

    // @title: title of the inserted image;
    // @path: the image folder path to insert the image in;
    // @image: the image to be inserted;
    void insertImageFromQImage(const QString &title, const QString &path, const QImage &image);

    // Key press handlers.
    bool handleKeyTab(QKeyEvent *p_event);
    bool handleKeyBackTab(QKeyEvent *p_event);
    bool handleKeyB(QKeyEvent *p_event);
    bool handleKeyH(QKeyEvent *p_event);
    bool handleKeyI(QKeyEvent *p_event);
    bool handleKeyO(QKeyEvent *p_event);
    bool handleKeyU(QKeyEvent *p_event);
    bool handleKeyW(QKeyEvent *p_event);
    bool handleKeyEsc(QKeyEvent *p_event);
    bool handleKeyReturn(QKeyEvent *p_event);
    bool handleKeyBracketLeft(QKeyEvent *p_event);
    bool insertTitle(int p_level);
    bool insertNewBlockWithIndent();
    bool insertListMarkAsPreviousLine();
    void deleteIndentAndListMark();

    // Check if @p_block is a auto list block.
    // @p_seq will be the seq number of the ordered list, or -1.
    // Returns true if it is an auto list block.
    bool isListBlock(const QTextBlock &p_block, int *p_seq = NULL);

    // If the start of @p_block to postition @p_posInBlock are spaces.
    bool isSpaceToBlockStart(const QTextBlock &p_block, int p_posInBlock);

    // Change the sequence number of a list block.
    void changeListBlockSeqNumber(QTextBlock &p_block, int p_seq);

    // The cursor position after auto indent or auto list.
    // It will be -1 if last key press do not trigger the auto indent or auto list.
    int m_autoIndentPos;

    static const QString c_defaultImageTitle;
};

#endif // VMDEDITOPERATIONS_H
