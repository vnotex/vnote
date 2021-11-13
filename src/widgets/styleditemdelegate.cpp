#include "styleditemdelegate.h"

#include <QPainter>
#include <QListWidgetItem>
#include <QTextDocument>
#include <QApplication>
#include <QStyle>
#include <QAbstractTextDocumentLayout>

#include <core/vnotex.h>
#include <core/thememgr.h>
#include "listwidget.h"
#include "treewidget.h"
#include "simplesegmenthighlighter.h"

using namespace vnotex;

StyledItemDelegateListWidget::StyledItemDelegateListWidget(const ListWidget *p_listWidget)
{
    Q_UNUSED(p_listWidget);
}


StyledItemDelegateTreeWidget::StyledItemDelegateTreeWidget(const TreeWidget *p_treeWidget)
{
    Q_UNUSED(p_treeWidget);
}


QBrush StyledItemDelegate::s_highlightForeground;

QBrush StyledItemDelegate::s_highlightBackground;

StyledItemDelegate::StyledItemDelegate(const QSharedPointer<StyledItemDelegateInterface> &p_interface,
                                       DelegateFlags p_flags,
                                       QObject *p_parent)
    : QStyledItemDelegate(p_parent),
      m_interface(p_interface),
      m_flags(p_flags)
{
    initialize();

    if (m_flags & DelegateFlag::Highlights) {
        m_document = new QTextDocument(this);
        m_highlighter = new SimpleSegmentHighlighter(m_document);
        m_highlighter->setHighlightFormat(s_highlightForeground, s_highlightBackground);
    }
}

void StyledItemDelegate::initialize()
{
    static bool initialized = false;
    if (!initialized) {
        initialized = true;

        const auto &themeMgr = VNoteX::getInst().getThemeMgr();
        s_highlightForeground = QColor(themeMgr.paletteColor(QStringLiteral("widgets#styleditemdelegate#highlight#fg")));
        s_highlightBackground = QColor(themeMgr.paletteColor(QStringLiteral("widgets#styleditemdelegate#highlight#bg")));
    }
}

void StyledItemDelegate::paint(QPainter *p_painter,
                               const QStyleOptionViewItem &p_option,
                               const QModelIndex &p_index) const
{
    // [Qt's BUG] Qt does not draw the background from Qt::BackgroundRole. Do it manually.
    auto bgBrushVal = p_index.data(Qt::BackgroundRole);
    if (bgBrushVal.canConvert<QBrush>()) {
        auto brush = qvariant_cast<QBrush>(bgBrushVal);
        if (brush.style() != Qt::NoBrush) {
            p_painter->fillRect(p_option.rect, brush);
        }
    }

    if (m_flags & DelegateFlag::Highlights) {
        const auto value = p_index.data(HighlightsRole);
        if (value.canConvert<QList<Segment>>()) {
            auto segments = value.value<QList<Segment>>();
            if (!segments.isEmpty()) {
                paintWithHighlights(p_painter, p_option, p_index, segments);
                return;
            }
        }
    }

    QStyledItemDelegate::paint(p_painter, p_option, p_index);
}

static void drawContents(const QStyleOptionViewItem &p_option,
                         QTextDocument *p_doc,
                         QPainter *p_painter,
                         const QRectF &p_rect)
{
    // From qtbase/src/gui/text/qtextdocument.cpp.

    p_painter->save();

    QAbstractTextDocumentLayout::PaintContext ctx;
    if (p_rect.isValid()) {
        p_painter->setClipRect(p_rect);
        ctx.clip = p_rect;
    }

    // Update palette.
    ctx.palette.setBrush(QPalette::Text, p_option.palette.brush(QPalette::Text));

    p_doc->documentLayout()->draw(p_painter, ctx);

    p_painter->restore();
}

void StyledItemDelegate::paintWithHighlights(QPainter *p_painter,
                                             const QStyleOptionViewItem &p_option,
                                             const QModelIndex &p_index,
                                             const QList<Segment> &p_segments) const
{
    QStyleOptionViewItem opt(p_option);
    initStyleOption(&opt, p_index);

    m_highlighter->setSegments(p_segments);
    m_document->clear();
    m_document->setDefaultFont(opt.font);
    m_document->setPlainText(opt.text);

    p_painter->save();

    // Draw the item without text.
    opt.text = "";
    auto style = opt.widget ? opt.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, p_painter, opt.widget);

    // Draw the text via QTextDocument.
    p_painter->translate(opt.rect.left(), opt.rect.top());
    const QRect clip(0, 0, opt.rect.width(), opt.rect.height());
    drawContents(opt, m_document, p_painter, clip);

    p_painter->restore();
}

QSize StyledItemDelegate::sizeHint(const QStyleOptionViewItem &p_option, const QModelIndex &p_index) const
{
    if (m_flags & DelegateFlag::Highlights) {
        const auto value = p_index.data(HighlightsRole);
        if (value.canConvert<QList<Segment>>()) {
            auto segments = value.value<QList<Segment>>();
            if (!segments.isEmpty()) {
                QStyleOptionViewItem opt(p_option);
                initStyleOption(&opt, p_index);

                m_document->setPlainText(opt.text);
                return QSize(m_document->idealWidth(), m_document->size().height());

            }
        }
    }

    return QStyledItemDelegate::sizeHint(p_option, p_index);
}
