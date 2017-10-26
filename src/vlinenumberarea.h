#ifndef VLINENUMBERAREA_H
#define VLINENUMBERAREA_H

#include <QWidget>
#include <QColor>

class QPaintEvent;
class QTextDocument;


enum class LineNumberType
{
    None = 0,
    Absolute,
    Relative,
    CodeBlock,
    Invalid
};


class VTextEditWithLineNumber
{
public:
    virtual ~VTextEditWithLineNumber() {}

    virtual void paintLineNumberArea(QPaintEvent *p_event) = 0;
};


// To use VLineNumberArea, the editor should implement VTextEditWithLineNumber.
class VLineNumberArea : public QWidget
{
    Q_OBJECT
public:
    VLineNumberArea(VTextEditWithLineNumber *p_editor,
                    const QTextDocument *p_document,
                    int p_digitWidth,
                    int p_digitHeight,
                    QWidget *p_parent = nullptr);

    QSize sizeHint() const Q_DECL_OVERRIDE
    {
        return QSize(calculateWidth(), 0);
    }

    int calculateWidth() const;

    int getDigitHeight() const
    {
        return m_digitHeight;
    }

    const QColor &getBackgroundColor() const;
    void setBackgroundColor(const QColor &p_color);

    const QColor &getForegroundColor() const;
    void setForegroundColor(const QColor &p_color);

protected:
    void paintEvent(QPaintEvent *p_event) Q_DECL_OVERRIDE
    {
        m_editor->paintLineNumberArea(p_event);
    }

private:
    VTextEditWithLineNumber *m_editor;
    const QTextDocument *m_document;
    int m_width;
    int m_blockCount;
    int m_digitWidth;
    int m_digitHeight;
    QColor m_foregroundColor;
    QColor m_backgroundColor;
};

inline const QColor &VLineNumberArea::getBackgroundColor() const
{
    return m_backgroundColor;
}

inline void VLineNumberArea::setBackgroundColor(const QColor &p_color)
{
    m_backgroundColor = p_color;
}

inline const QColor &VLineNumberArea::getForegroundColor() const
{
    return m_foregroundColor;
}

inline void VLineNumberArea::setForegroundColor(const QColor &p_color)
{
    m_foregroundColor = p_color;
}

#endif // VLINENUMBERAREA_H
