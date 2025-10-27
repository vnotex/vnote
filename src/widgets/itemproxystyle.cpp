#include "itemproxystyle.h"

#include <QDebug>
#include <QPainter>
#include <QStyleOptionViewItem>
#include <QTextLayout>
#include <QTextOption>

#include "styleditemdelegate.h"

using namespace vnotex;

ItemProxyStyle::ItemProxyStyle(QStyle *p_style) : QProxyStyle(p_style) {}

void ItemProxyStyle::drawControl(QStyle::ControlElement p_element, const QStyleOption *p_option,
                                 QPainter *p_painter, const QWidget *p_widget) const {
  if (p_element == QStyle::CE_ItemViewItem) {
    if (drawItemViewItem(p_option, p_painter, p_widget)) {
      return;
    }
  }

  QProxyStyle::drawControl(p_element, p_option, p_painter, p_widget);
}

bool ItemProxyStyle::drawItemViewItem(const QStyleOption *p_option, QPainter *p_painter,
                                      const QWidget *p_widget) const {
  const QStyleOptionViewItem *vopt = qstyleoption_cast<const QStyleOptionViewItem *>(p_option);
  if (!vopt) {
    return false;
  }

  const auto value = vopt->index.data(HighlightsRole);
  if (!value.canConvert<QList<Segment>>()) {
    return false;
  }

  auto segments = value.value<QList<Segment>>();
  if (segments.isEmpty()) {
    return false;
  }

  // Copied from qtbase/src/widgets/styles/qcommonstyle.cpp.

  p_painter->save();
  p_painter->setClipRect(vopt->rect);
  QRect checkRect = proxy()->subElementRect(SE_ItemViewItemCheckIndicator, vopt, p_widget);
  QRect iconRect = proxy()->subElementRect(SE_ItemViewItemDecoration, vopt, p_widget);
  QRect textRect = proxy()->subElementRect(SE_ItemViewItemText, vopt, p_widget);

  // Draw the background.
  proxy()->drawPrimitive(PE_PanelItemViewItem, vopt, p_painter, p_widget);

  // Draw the check mark.
  if (vopt->features & QStyleOptionViewItem::HasCheckIndicator) {
    QStyleOptionViewItem option(*vopt);
    option.rect = checkRect;
    option.state = option.state & ~QStyle::State_HasFocus;
    switch (vopt->checkState) {
    case Qt::Unchecked:
      option.state |= QStyle::State_Off;
      break;
    case Qt::PartiallyChecked:
      option.state |= QStyle::State_NoChange;
      break;
    case Qt::Checked:
      option.state |= QStyle::State_On;
      break;
    }
    proxy()->drawPrimitive(QStyle::PE_IndicatorItemViewItemCheck, &option, p_painter, p_widget);
  }

  // Draw the icon.
  QIcon::Mode mode = QIcon::Normal;
  if (!(vopt->state & QStyle::State_Enabled)) {
    mode = QIcon::Disabled;
  } else if (vopt->state & QStyle::State_Selected) {
    mode = QIcon::Selected;
  }
  QIcon::State state = vopt->state & QStyle::State_Open ? QIcon::On : QIcon::Off;
  vopt->icon.paint(p_painter, iconRect, vopt->decorationAlignment, mode, state);

  // Draw the text.
  if (!vopt->text.isEmpty()) {
    QPalette::ColorGroup cg =
        vopt->state & QStyle::State_Enabled ? QPalette::Normal : QPalette::Disabled;
    if (cg == QPalette::Normal && !(vopt->state & QStyle::State_Active)) {
      cg = QPalette::Inactive;
    }
    if (vopt->state & QStyle::State_Selected) {
      p_painter->setPen(vopt->palette.color(cg, QPalette::HighlightedText));
    } else {
      p_painter->setPen(vopt->palette.color(cg, QPalette::Text));
    }
    if (vopt->state & QStyle::State_Editing) {
      p_painter->setPen(vopt->palette.color(cg, QPalette::Text));
      p_painter->drawRect(textRect.adjusted(0, 0, -1, -1));
    }

    viewItemDrawText(p_painter, vopt, textRect);
  }

  // Draw the focus rect.
  if (vopt->state & QStyle::State_HasFocus) {
    QStyleOptionFocusRect o;
    o.QStyleOption::operator=(*vopt);
    o.rect = proxy()->subElementRect(SE_ItemViewItemFocusRect, vopt, p_widget);
    o.state |= QStyle::State_KeyboardFocusChange;
    o.state |= QStyle::State_Item;
    QPalette::ColorGroup cg =
        (vopt->state & QStyle::State_Enabled) ? QPalette::Normal : QPalette::Disabled;
    o.backgroundColor = vopt->palette.color(
        cg, (vopt->state & QStyle::State_Selected) ? QPalette::Highlight : QPalette::Window);
    proxy()->drawPrimitive(QStyle::PE_FrameFocusRect, &o, p_painter, p_widget);
  }

  p_painter->restore();

  return true;
}

static QSizeF viewItemTextLayout(QTextLayout &textLayout, int lineWidth, int maxHeight = -1,
                                 int *lastVisibleLine = nullptr) {
  if (lastVisibleLine)
    *lastVisibleLine = -1;
  qreal height = 0;
  qreal widthUsed = 0;
  textLayout.beginLayout();
  int i = 0;
  while (true) {
    QTextLine line = textLayout.createLine();
    if (!line.isValid())
      break;
    line.setLineWidth(lineWidth);
    line.setPosition(QPointF(0, height));
    height += line.height();
    widthUsed = qMax(widthUsed, line.naturalTextWidth());
    // we assume that the height of the next line is the same as the current one
    if (maxHeight > 0 && lastVisibleLine && height + line.height() > maxHeight) {
      const QTextLine nextLine = textLayout.createLine();
      *lastVisibleLine = nextLine.isValid() ? i : -1;
      break;
    }
    ++i;
  }
  textLayout.endLayout();
  return QSizeF(widthUsed, height);
}

void ItemProxyStyle::viewItemDrawText(QPainter *p_painter, const QStyleOptionViewItem *p_option,
                                      const QRect &p_rect) const {
  // Copied from qtbase/src/widgets/styles/qcommonstyle.cpp.

  const QWidget *widget = p_option->widget;
  const int textMargin = proxy()->pixelMetric(QStyle::PM_FocusFrameHMargin, 0, widget) + 1;
  // Remove width padding.
  QRect textRect = p_rect.adjusted(textMargin, 0, -textMargin, 0);
  const bool wrapText = p_option->features & QStyleOptionViewItem::WrapText;
  QTextOption textOption;
  textOption.setWrapMode(wrapText ? QTextOption::WordWrap : QTextOption::ManualWrap);
  textOption.setTextDirection(p_option->direction);
  textOption.setAlignment(QStyle::visualAlignment(p_option->direction, p_option->displayAlignment));
  QPointF paintPosition;
  const QString newText = calculateElidedText(p_option->text, textOption, p_option->font, textRect,
                                              p_option->displayAlignment, p_option->textElideMode,
                                              0, true, &paintPosition);
  QTextLayout textLayout(newText, p_option->font);
  textLayout.setTextOption(textOption);
  viewItemTextLayout(textLayout, textRect.width());
  textLayout.draw(p_painter, paintPosition);
}

QString ItemProxyStyle::calculateElidedText(const QString &text, const QTextOption &textOption,
                                            const QFont &font, const QRect &textRect,
                                            const Qt::Alignment valign,
                                            Qt::TextElideMode textElideMode, int flags,
                                            bool lastVisibleLineShouldBeElided,
                                            QPointF *paintStartPosition) const {
  // Copied from qtbase/src/widgets/styles/qcommonstyle.cpp.

  QTextLayout textLayout(text, font);
  textLayout.setTextOption(textOption);
  // In AlignVCenter mode when more than one line is displayed and the height only allows
  // some of the lines it makes no sense to display those. From a users perspective it makes
  // more sense to see the start of the text instead something inbetween.
  const bool vAlignmentOptimization = paintStartPosition && valign.testFlag(Qt::AlignVCenter);
  int lastVisibleLine = -1;
  viewItemTextLayout(textLayout, textRect.width(), vAlignmentOptimization ? textRect.height() : -1,
                     &lastVisibleLine);
  const QRectF boundingRect = textLayout.boundingRect();
  // don't care about LTR/RTL here, only need the height
  const QRect layoutRect =
      QStyle::alignedRect(Qt::LayoutDirectionAuto, valign, boundingRect.size().toSize(), textRect);
  if (paintStartPosition)
    *paintStartPosition = QPointF(textRect.x(), layoutRect.top());
  QString ret;
  qreal height = 0;
  const int lineCount = textLayout.lineCount();
  for (int i = 0; i < lineCount; ++i) {
    const QTextLine line = textLayout.lineAt(i);
    height += line.height();
    // above visible rect
    if (height + layoutRect.top() <= textRect.top()) {
      if (paintStartPosition)
        paintStartPosition->ry() += line.height();
      continue;
    }
    const int start = line.textStart();
    const int length = line.textLength();
    const bool drawElided = line.naturalTextWidth() > textRect.width();
    bool elideLastVisibleLine = lastVisibleLine == i;
    if (!drawElided && i + 1 < lineCount && lastVisibleLineShouldBeElided) {
      const QTextLine nextLine = textLayout.lineAt(i + 1);
      const int nextHeight = height + nextLine.height() / 2;
      // elide when less than the next half line is visible
      if (nextHeight + layoutRect.top() > textRect.height() + textRect.top())
        elideLastVisibleLine = true;
    }
    QString text = textLayout.text().mid(start, length);
    if (drawElided || elideLastVisibleLine) {
      Q_ASSERT(false);
      if (elideLastVisibleLine) {
        if (text.endsWith(QChar::LineSeparator))
          text.chop(1);
        text += QChar(0x2026);
      }
      /* TODO: QStackTextEngine is a private class.
      const QStackTextEngine engine(text, font);
      ret += engine.elidedText(textElideMode, textRect.width(), flags);
      */
      Q_UNUSED(flags);
      Q_UNUSED(textElideMode);
      ret += text;
      // no newline for the last line (last visible or real)
      // sometimes drawElided is true but no eliding is done so the text ends
      // with QChar::LineSeparator - don't add another one. This happened with
      // arabic text in the testcase for QTBUG-72805
      if (i < lineCount - 1 && !ret.endsWith(QChar::LineSeparator))
        ret += QChar::LineSeparator;
    } else {
      ret += text;
    }
    // below visible text, can stop
    if ((height + layoutRect.top() >= textRect.bottom()) ||
        (lastVisibleLine >= 0 && lastVisibleLine == i))
      break;
  }
  return ret;
}
