#ifndef VPLAINTEXTEDIT_H
#define VPLAINTEXTEDIT_H

#include <QPlainTextEdit>
#include <QPlainTextDocumentLayout>
#include <QTextBlock>

#include "vlinenumberarea.h"

class QTextDocument;
class VImageResourceManager;
class QPaintEvent;
class QPainter;
class QResizeEvent;
class VPlainTextDocumentLayout;


struct VBlockImageInfo
{
public:
    VBlockImageInfo()
        : m_blockNumber(-1), m_margin(0), m_imageWidth(0), m_imageHeight(0)
    {
    }

    VBlockImageInfo(int p_blockNumber,
                    const QString &p_imageName,
                    int p_margin = 0)
        : m_blockNumber(p_blockNumber),
          m_imageName(p_imageName),
          m_margin(p_margin),
          m_imageWidth(0),
          m_imageHeight(0)
    {
    }

    // Block number.
    int m_blockNumber;

    // The name of the image corresponding to this block.
    QString m_imageName;

    // Left margin of the image.
    int m_margin;

private:
    // Width and height of the image display.
    int m_imageWidth;
    int m_imageHeight;

    friend class VImageResourceManager;
    friend class VPlainTextEdit;
    friend class VPlainTextDocumentLayout;
};


class VPlainTextEdit : public QPlainTextEdit, public VTextEditWithLineNumber
{
    Q_OBJECT
public:
    explicit VPlainTextEdit(QWidget *p_parent = nullptr);

    explicit VPlainTextEdit(const QString &p_text, QWidget *p_parent = nullptr);

    virtual ~VPlainTextEdit();

    // Update images of these given blocks.
    // Images of blocks not given here will be clear.
    void updateBlockImages(const QVector<VBlockImageInfo> &p_blocksInfo);

    void clearBlockImages();

    // Whether the resoruce manager contains image of name @p_imageName.
    bool containsImage(const QString &p_imageName) const;

    // Add an image to the resources.
    void addImage(const QString &p_imageName, const QPixmap &p_image);

    void setBlockImageEnabled(bool p_enabled);

    void setImageWidthConstrainted(bool p_enabled);

    void paintLineNumberArea(QPaintEvent *p_event) Q_DECL_OVERRIDE;

    void setLineNumberType(LineNumberType p_type);

    void setLineNumberColor(const QColor &p_foreground, const QColor &p_background);

    // The minimum width of an image in pixels.
    static const int c_minimumImageWidth;

protected:
    // Most logics are copied from QPlainTextEdit.
    // Differences: draw images for blocks with preview image.
    void paintEvent(QPaintEvent *p_event) Q_DECL_OVERRIDE;

    void resizeEvent(QResizeEvent *p_event) Q_DECL_OVERRIDE;

private slots:
    // Update viewport margin to hold the line number area.
    void updateLineNumberAreaMargin();

    void updateLineNumberArea();

private:
    void init();

    // @p_blockRect: the content rect of @p_block.
    void drawImageOfBlock(const QTextBlock &p_block,
                          QPainter *p_painter,
                          const QRectF &p_blockRect);

    QRectF originalBlockBoundingRect(const QTextBlock &p_block) const;

    VPlainTextDocumentLayout *getLayout() const;

    void updateImageWidth();

    // Widget to display line number area.
    VLineNumberArea *m_lineNumberArea;

    VImageResourceManager *m_imageMgr;

    bool m_blockImageEnabled;

    // Whether constraint the width of image to the width of the viewport.
    bool m_imageWidthConstrainted;

    // Maximum width of the images.
    int m_maximumImageWidth;

    LineNumberType m_lineNumberType;
};


class VPlainTextDocumentLayout : public QPlainTextDocumentLayout
{
    Q_OBJECT
public:
    explicit VPlainTextDocumentLayout(QTextDocument *p_document,
                                      VImageResourceManager *p_imageMgr,
                                      bool p_blockImageEnabled = false);

    // Will adjust the rect if there is an image for this block.
    QRectF blockBoundingRect(const QTextBlock &p_block) const Q_DECL_OVERRIDE;

    QRectF frameBoundingRect(QTextFrame *p_frame) const Q_DECL_OVERRIDE;

    QSizeF documentSize() const Q_DECL_OVERRIDE;

    void setBlockImageEnabled(bool p_enabled);

    void setMaximumImageWidth(int p_width);

private:
    VImageResourceManager *m_imageMgr;

    bool m_blockImageEnabled;

    int m_maximumImageWidth;
};

inline void VPlainTextDocumentLayout::setBlockImageEnabled(bool p_enabled)
{
    m_blockImageEnabled = p_enabled;
}

inline void VPlainTextDocumentLayout::setMaximumImageWidth(int p_width)
{
    m_maximumImageWidth = p_width;
}

inline void VPlainTextEdit::setLineNumberType(LineNumberType p_type)
{
    if (p_type == m_lineNumberType) {
        return;
    }

    m_lineNumberType = p_type;

    updateLineNumberArea();
}

inline void VPlainTextEdit::setLineNumberColor(const QColor &p_foreground,
                                               const QColor &p_background)
{
    m_lineNumberArea->setForegroundColor(p_foreground);
    m_lineNumberArea->setBackgroundColor(p_background);
}

#endif // VPLAINTEXTEDIT_H
