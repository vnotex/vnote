#ifndef VMDEDIT_H
#define VMDEDIT_H

#include "vedit.h"
#include <QVector>
#include <QString>
#include <QColor>
#include <QClipboard>
#include <QImage>
#include "vtableofcontent.h"
#include "veditoperations.h"
#include "vconfigmanager.h"
#include "utils/vutils.h"

class HGMarkdownHighlighter;
class VCodeBlockHighlightHelper;
class VDocument;
class VImagePreviewer;

class VMdEdit : public VEdit
{
    Q_OBJECT
public:
    VMdEdit(VFile *p_file, VDocument *p_vdoc, MarkdownConverterType p_type,
            QWidget *p_parent = 0);
    void beginEdit() Q_DECL_OVERRIDE;
    void endEdit() Q_DECL_OVERRIDE;
    void saveFile() Q_DECL_OVERRIDE;
    void reloadFile() Q_DECL_OVERRIDE;

    // An image has been inserted. The image is relative.
    // @p_path is the absolute path of the inserted image.
    void imageInserted(const QString &p_path);

    // Scroll to header @p_blockNumber.
    // Return true if @p_blockNumber is valid to scroll to.
    bool scrollToHeader(int p_blockNumber);

    // Like toPlainText(), but remove image preview characters.
    QString toPlainTextWithoutImg();

public slots:
    bool jumpTitle(bool p_forward, int p_relativeLevel, int p_repeat) Q_DECL_OVERRIDE;

signals:
    // Signal when headers change.
    void headersChanged(const QVector<VTableOfContentItem> &p_headers);

    // Signal when current header change.
    void currentHeaderChanged(int p_blockNumber);

    // Signal when the status of VMdEdit changed.
    // Will be emitted by VImagePreviewer for now.
    void statusChanged();

private slots:
    // Update m_headers according to elements.
    void updateHeaders(const QVector<VElementRegion> &p_headerRegions);

    // Update current header according to cursor position.
    // When there is no header in current cursor, will signal an invalid header.
    void updateCurrentHeader();

    void handleClipboardChanged(QClipboard::Mode p_mode);

protected:
    void keyPressEvent(QKeyEvent *event) Q_DECL_OVERRIDE;
    bool canInsertFromMimeData(const QMimeData *source) const Q_DECL_OVERRIDE;
    void insertFromMimeData(const QMimeData *source) Q_DECL_OVERRIDE;
    void updateFontAndPalette() Q_DECL_OVERRIDE;
    void resizeEvent(QResizeEvent *p_event) Q_DECL_OVERRIDE;

private:
    struct Region
    {
        Region() : m_startPos(-1), m_endPos(-1)
        {
        }

        Region(int p_start, int p_end)
            : m_startPos(p_start), m_endPos(p_end)
        {
        }

        int m_startPos;
        int m_endPos;
    };

    // Get the initial images from file before edit.
    void initInitImages();

    // Clear two kind of images according to initial images and current images:
    // 1. Newly inserted images which are deleted later;
    // 2. Initial images which are deleted;
    void clearUnusedImages();

    // There is a QChar::ObjectReplacementCharacter (and maybe some spaces)
    // in the selection. Get the QImage.
    QImage tryGetSelectedImage();

    QString getPlainTextWithoutPreviewImage() const;

    // Try to get all the regions of preview image within @p_block.
    // Returns false if preview image is not ready yet.
    bool getPreviewImageRegionOfBlock(const QTextBlock &p_block,
                                      QVector<Region> &p_regions) const;

    void finishOneAsyncJob(int p_idx);

    // Index in m_headers of current header which contains the cursor.
    int indexOfCurrentHeader() const;

    HGMarkdownHighlighter *m_mdHighlighter;
    VCodeBlockHighlightHelper *m_cbHighlighter;
    VImagePreviewer *m_imagePreviewer;

    // Image links inserted while editing.
    QVector<ImageLink> m_insertedImages;

    // Image links right at the beginning of the edit.
    QVector<ImageLink> m_initImages;

    // Mainly used for title jump.
    QVector<VTableOfContentItem> m_headers;

    bool m_freshEdit;

    QVector<bool> m_finishedAsyncJobs;

    static const int c_numberOfAysncJobs;
};

#endif // VMDEDIT_H
