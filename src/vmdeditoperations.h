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
    VMdEditOperations(VEditor *p_editor, VFile *p_file);

    bool insertImageFromMimeData(const QMimeData *source) Q_DECL_OVERRIDE;

    bool insertImage() Q_DECL_OVERRIDE;

    bool handleKeyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;

    bool insertImageFromURL(const QUrl &p_imageUrl) Q_DECL_OVERRIDE;

    bool insertLink(const QString &p_linkText,
                    const QString &p_linkUrl) Q_DECL_OVERRIDE;

    // Insert decoration markers or decorate selected text.
    // If it is Vim Normal mode, change to Insert mode first.
    void decorateText(TextDecoration p_decoration, int p_level = -1) Q_DECL_OVERRIDE;

    bool insertImageLink(const QString &p_linkText, const QString &p_linkUrl);

    // @p_urlInLink and @p_destImagePath will be empty on failure.
    void insertImageFromPath(const QString &p_title,
                             const QString &p_folderPath,
                             const QString &p_folderInLink,
                             const QString &p_srcImagePath,
                             bool p_insertText,
                             QString &p_destImagePath,
                             QString &p_urlInLink);

private:
    // Insert image from @p_srcImagePath as to @p_folderPath.
    // @p_folderInLink: the folder part in the image link.
    void insertImageFromPath(const QString &p_title,
                             const QString &p_folderPath,
                             const QString &p_folderInLink,
                             const QString &p_srcImagePath);

    // @title: title of the inserted image;
    // @path: the image folder path to insert the image in;
    // @folderInLink: the folder part in the image link.
    // @image: the image to be inserted;
    void insertImageFromQImage(const QString &title, const QString &path,
                               const QString &folderInLink, const QImage &image);

    // Key press handlers.
    bool handleKeyTab(QKeyEvent *p_event);
    bool handleKeyBackTab(QKeyEvent *p_event);
    bool handleKeyH(QKeyEvent *p_event);
    bool handleKeyU(QKeyEvent *p_event);
    bool handleKeyW(QKeyEvent *p_event);
    bool handleKeyEsc(QKeyEvent *p_event);
    bool handleKeyReturn(QKeyEvent *p_event);
    bool handleKeyBracketLeft(QKeyEvent *p_event);

    // Change the sequence number of a list block.
    void changeListBlockSeqNumber(QTextBlock &p_block, int p_seq);

    // Insert bold marker or set selected text bold.
    void decorateBold();

    // Insert italic marker or set selected text italic.
    void decorateItalic();

    // Insert inline-code marker or set selected text inline-coded.
    void decorateInlineCode();

    // Insert inline-code marker or set selected text inline-coded.
    void decorateCodeBlock();

    // Insert strikethrough marker or set selected text strikethrough.
    void decorateStrikethrough();

    // Insert title of level @p_level.
    // Will detect if current block already has some leading #s. If yes,
    // will delete it and insert the correct #s.
    // @p_level: 0 to cancel title.
    bool decorateHeading(int p_level);

    // The cursor position after auto indent or auto list.
    // It will be -1 if last key press do not trigger the auto indent or auto list.
    int m_autoIndentPos;

    static const QString c_defaultImageTitle;
};

#endif // VMDEDITOPERATIONS_H
