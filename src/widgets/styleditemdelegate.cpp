#include "styleditemdelegate.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QListWidgetItem>
#include <QPainter>
#include <QStyle>
#include <QTextDocument>
#include <QtMath>

#include "listwidget.h"
#include "simplesegmenthighlighter.h"
#include "treewidget.h"
#include <gui/utils/itemviewutils.h>

using namespace vnotex;

StyledItemDelegateListWidget::StyledItemDelegateListWidget(const ListWidget *p_listWidget) {
  Q_UNUSED(p_listWidget);
}

StyledItemDelegateTreeWidget::StyledItemDelegateTreeWidget(const TreeWidget *p_treeWidget) {
  Q_UNUSED(p_treeWidget);
}

StyledItemDelegate::StyledItemDelegate(
    const QSharedPointer<StyledItemDelegateInterface> &p_interface, DelegateFlags p_flags,
    const QBrush &p_highlightFg, const QBrush &p_highlightBg, QObject *p_parent)
    : QStyledItemDelegate(p_parent), m_interface(p_interface), m_flags(p_flags),
      m_highlightForeground(p_highlightFg), m_highlightBackground(p_highlightBg) {
  if (m_flags & DelegateFlag::Highlights) {
    m_document = new QTextDocument(this);
    // The theme owns the padding (QSS `::item`), so the document must not carry
    // its own: QTextDocument defaults to a 4px margin on every side, which would
    // be counted on top of ItemViewUtils::verticalChrome() in sizeHint() and
    // would also pin the text to a hardcoded inset while painting.
    m_document->setDocumentMargin(0);
    m_highlighter = new SimpleSegmentHighlighter(m_document);
    m_highlighter->setHighlightFormat(m_highlightForeground, m_highlightBackground);
  }
}

void StyledItemDelegate::paint(QPainter *p_painter, const QStyleOptionViewItem &p_option,
                               const QModelIndex &p_index) const {
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

static void drawContents(const QStyleOptionViewItem &p_option, QTextDocument *p_doc,
                         QPainter *p_painter, const QRectF &p_rect) {
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
                                             const QList<Segment> &p_segments) const {
  QStyleOptionViewItem opt(p_option);
  initStyleOption(&opt, p_index);

  m_highlighter->setSegments(p_segments);
  m_document->clear();
  m_document->setDefaultFont(opt.font);
  m_document->setPlainText(opt.text);

  p_painter->save();

  auto style = opt.widget ? opt.widget->style() : QApplication::style();

  // Where CE_ItemViewItem would have put the glyphs. SE_ItemViewItemText is only
  // the text ALLOCATION; QCommonStyle::viewItemDrawText() then insets it by
  // PM_FocusFrameHMargin + 1 on both edges before drawing. Reproducing both steps
  // (rather than translating to opt.rect plus a hardcoded document margin) is
  // what keeps a highlighted row aligned with its unhighlighted siblings, both
  // horizontally and under any theme's ::item padding.
  const int textMargin = style->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, opt.widget) + 1;
  const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget)
                             .adjusted(textMargin, 0, -textMargin, 0);

  // Draw the item without text.
  opt.text = "";
  style->drawControl(QStyle::CE_ItemViewItem, &opt, p_painter, opt.widget);

  // Draw the text via QTextDocument, vertically centred in the text rect the way
  // CE_ItemViewItem centres a single line.
  const int docHeight = qCeil(m_document->size().height());
  p_painter->translate(textRect.left(),
                       textRect.top() + qMax(0, (textRect.height() - docHeight) / 2));
  const QRect clip(0, 0, textRect.width(), docHeight);
  drawContents(opt, m_document, p_painter, clip);

  p_painter->restore();
}

QSize StyledItemDelegate::sizeHint(const QStyleOptionViewItem &p_option,
                                   const QModelIndex &p_index) const {
  if (m_flags & DelegateFlag::Highlights) {
    const auto value = p_index.data(HighlightsRole);
    if (value.canConvert<QList<Segment>>()) {
      auto segments = value.value<QList<Segment>>();
      if (!segments.isEmpty()) {
        QStyleOptionViewItem opt(p_option);
        initStyleOption(&opt, p_index);

        // Measure with the same font the paint path will use, or the size hint
        // and the rendering disagree whenever Qt::FontRole is set.
        m_document->setDefaultFont(opt.font);
        m_document->setPlainText(opt.text);
        // The QTextDocument owns the content height; the theme owns the padding
        // (see gui/utils/itemviewutils.h). Without this a highlighted row is
        // shorter than its unhighlighted siblings in the same tree, because
        // QStyledItemDelegate::sizeHint() routes through CT_ItemViewItem and
        // this branch does not. The document carries no margin of its own (see
        // the constructor), so nothing is double-counted.
        const int docHeight = qCeil(m_document->size().height());
        // Width comes from the base hint, which is the only thing that accounts
        // for the decoration, the check indicator and the theme's HORIZONTAL
        // ::item padding; the document only knows about the text.
        const QSize baseHint = QStyledItemDelegate::sizeHint(p_option, p_index);
        return QSize(qMax(baseHint.width(), qCeil(m_document->idealWidth())),
                     qMax(docHeight, opt.fontMetrics.height()) +
                         ItemViewUtils::verticalChrome(opt));
      }
    }
  }

  return QStyledItemDelegate::sizeHint(p_option, p_index);
}
