#ifndef VTEXTEDIT_H
#define VTEXTEDIT_H

#include <QTextEdit>
#include <QTextBlock>

#include "vlinenumberarea.h"
#include "vtextdocumentlayout.h"

class VTextDocumentLayout;
class QPainter;
class QResizeEvent;
class VImageResourceManager2;


class VTextEdit : public QTextEdit, public VTextEditWithLineNumber
{
    Q_OBJECT
public:
    explicit VTextEdit(QWidget *p_parent = nullptr);

    explicit VTextEdit(const QString &p_text, QWidget *p_parent = nullptr);

    virtual ~VTextEdit();

    void init();

    void setLineLeading(qreal p_leading);

    void paintLineNumberArea(QPaintEvent *p_event) Q_DECL_OVERRIDE;

    void setLineNumberType(LineNumberType p_type);

    void setLineNumberColor(const QColor &p_foreground, const QColor &p_background);

    int firstVisibleBlockNumber() const;

    QTextBlock firstVisibleBlock() const;

    QTextBlock lastVisibleBlock() const;

    void visibleBlockRange(int &p_first, int &p_last) const;

    void clearBlockImages();

    // Whether the resoruce manager contains image of name @p_imageName.
    bool containsImage(const QString &p_imageName) const;

    // Get the image size from the resource manager.
    QSize imageSize(const QString &p_imageName) const;

    // Add an image to the resources.
    void addImage(const QString &p_imageName, const QPixmap &p_image);

    // Remove an image from the resources.
    void removeImage(const QString &p_imageName);

    void setBlockImageEnabled(bool p_enabled);

    void setImageWidthConstrainted(bool p_enabled);

    void setImageLineColor(const QColor &p_color);

    void relayout(const OrderedIntSet &p_blocks);

    void setCursorBlockMode(CursorBlock p_mode);

    void setHighlightCursorLineBlockEnabled(bool p_enabled);

    void setCursorLineBlockBg(const QColor &p_bg);

    void relayout();

    void relayoutVisibleBlocks();

    void setDisplayScaleFactor(qreal p_factor);

protected:
    void resizeEvent(QResizeEvent *p_event) Q_DECL_OVERRIDE;

    // Return the Y offset of the content via the scrollbar.
    int contentOffsetY() const;

private slots:
    // Update viewport margin to hold the line number area.
    void updateLineNumberAreaMargin();

    void updateLineNumberArea();

private:
    VTextDocumentLayout *getLayout() const;

    VLineNumberArea *m_lineNumberArea;

    LineNumberType m_lineNumberType;

    VImageResourceManager2 *m_imageMgr;

    bool m_blockImageEnabled;

    CursorBlock m_cursorBlockMode;

    bool m_highlightCursorLineBlock;

    int m_defaultCursorWidth;
};

inline void VTextEdit::setLineNumberType(LineNumberType p_type)
{
    if (p_type == m_lineNumberType) {
        return;
    }

    m_lineNumberType = p_type;

    updateLineNumberArea();
}

inline void VTextEdit::setLineNumberColor(const QColor &p_foreground,
                                          const QColor &p_background)
{
    m_lineNumberArea->setForegroundColor(p_foreground);
    m_lineNumberArea->setBackgroundColor(p_background);
}
#endif // VTEXTEDIT_H
