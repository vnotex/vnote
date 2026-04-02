#include "searchresultdelegate.h"

#include <QApplication>
#include <QPainter>

#include <models/searchresultmodel.h>

using namespace vnotex;

SearchResultDelegate::SearchResultDelegate(QWidget *p_parent)
    : QStyledItemDelegate(p_parent) {}

SearchResultDelegate::~SearchResultDelegate() {}

void SearchResultDelegate::paint(QPainter *p_painter, const QStyleOptionViewItem &p_option,
                                 const QModelIndex &p_index) const {
  if (!p_index.isValid()) {
    return;
  }

  bool isFileResult = p_index.data(SearchResultModel::IsFileResultRole).toBool();
  if (isFileResult) {
    paintFileResult(p_painter, p_option, p_index);
  } else {
    paintLineResult(p_painter, p_option, p_index);
  }
}

QSize SearchResultDelegate::sizeHint(const QStyleOptionViewItem &p_option,
                                     const QModelIndex &p_index) const {
  Q_UNUSED(p_index);

  QFontMetrics fm(p_option.font);
  int height = fm.height() + m_vPadding * 2;
  return QSize(200, height);
}

void SearchResultDelegate::paintFileResult(QPainter *p_painter,
                                           const QStyleOptionViewItem &p_option,
                                           const QModelIndex &p_index) const {
  p_painter->save();

  const QWidget *widget = p_option.widget;
  QStyle *style = widget ? widget->style() : QApplication::style();

  // Draw selection/hover background
  style->drawPrimitive(QStyle::PE_PanelItemViewItem, &p_option, p_painter, widget);

  // File name in bold
  QString displayText = p_index.data(Qt::DisplayRole).toString();
  QFont boldFont = p_option.font;
  boldFont.setBold(true);

  QFontMetrics fm(boldFont);
  QRect textRect = p_option.rect;
  textRect.setLeft(textRect.left() + m_hPadding);
  textRect.setRight(textRect.right() - m_hPadding);

  // Draw match count badge on the right
  int matchCount = p_index.data(SearchResultModel::MatchCountRole).toInt();
  QString badge;
  if (matchCount > 0) {
    badge = QStringLiteral("[%1]").arg(matchCount);
  }

  QFontMetrics normalFm(p_option.font);
  int badgeWidth = 0;
  if (!badge.isEmpty()) {
    badgeWidth = normalFm.horizontalAdvance(badge) + m_hPadding;
  }

  // Draw file name (bold)
  QRect nameRect = textRect;
  nameRect.setRight(nameRect.right() - badgeWidth);
  QString elidedName = fm.elidedText(displayText, Qt::ElideMiddle, nameRect.width());

  QColor textColor = (p_option.state & QStyle::State_Selected)
                         ? p_option.palette.highlightedText().color()
                         : p_option.palette.text().color();

  p_painter->setPen(textColor);
  p_painter->setFont(boldFont);
  p_painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, elidedName);

  // Draw badge
  if (!badge.isEmpty()) {
    QRect badgeRect = textRect;
    badgeRect.setLeft(badgeRect.right() - badgeWidth);

    QColor badgeColor = textColor;
    badgeColor.setAlpha(150);
    p_painter->setPen(badgeColor);
    p_painter->setFont(p_option.font);
    p_painter->drawText(badgeRect, Qt::AlignRight | Qt::AlignVCenter, badge);
  }

  // Draw focus rect
  if (p_option.state & QStyle::State_HasFocus) {
    QStyleOptionFocusRect focusOpt;
    focusOpt.QStyleOption::operator=(p_option);
    focusOpt.rect = p_option.rect;
    focusOpt.state |= QStyle::State_KeyboardFocusChange;
    style->drawPrimitive(QStyle::PE_FrameFocusRect, &focusOpt, p_painter, widget);
  }

  p_painter->restore();
}

void SearchResultDelegate::paintLineResult(QPainter *p_painter,
                                           const QStyleOptionViewItem &p_option,
                                           const QModelIndex &p_index) const {
  p_painter->save();

  const QWidget *widget = p_option.widget;
  QStyle *style = widget ? widget->style() : QApplication::style();

  // Draw selection/hover background
  style->drawPrimitive(QStyle::PE_PanelItemViewItem, &p_option, p_painter, widget);

  int lineNumber = p_index.data(SearchResultModel::LineNumberRole).toInt();
  int colStart = p_index.data(SearchResultModel::ColumnStartRole).toInt();
  int colEnd = p_index.data(SearchResultModel::ColumnEndRole).toInt();
  QString lineText = p_index.data(Qt::DisplayRole).toString();

  QColor textColor = (p_option.state & QStyle::State_Selected)
                         ? p_option.palette.highlightedText().color()
                         : p_option.palette.text().color();

  QFontMetrics fm(p_option.font);
  QRect contentRect = p_option.rect;
  contentRect.setLeft(contentRect.left() + m_hPadding);
  contentRect.setRight(contentRect.right() - m_hPadding);

  // Draw line number prefix in gray
  QString linePrefix = QStringLiteral("%1: ").arg(lineNumber);
  int prefixWidth = fm.horizontalAdvance(linePrefix);

  QColor dimColor = Qt::gray;
  if (p_option.state & QStyle::State_Selected) {
    dimColor = textColor;
    dimColor.setAlpha(180);
  }

  QRect prefixRect = contentRect;
  prefixRect.setWidth(prefixWidth);

  p_painter->setPen(dimColor);
  p_painter->setFont(p_option.font);
  p_painter->drawText(prefixRect, Qt::AlignLeft | Qt::AlignVCenter, linePrefix);

  // Draw line text with match highlighting
  QRect lineRect = contentRect;
  lineRect.setLeft(prefixRect.right());

  if (colStart >= 0 && colEnd > colStart && colStart < lineText.length()) {
    // Text before match
    QString beforeMatch = lineText.left(colStart);
    int beforeWidth = fm.horizontalAdvance(beforeMatch);

    // Matched segment
    int clampedEnd = qMin(colEnd, lineText.length());
    QString matchText = lineText.mid(colStart, clampedEnd - colStart);
    int matchWidth = fm.horizontalAdvance(matchText);

    // Text after match
    QString afterMatch = lineText.mid(clampedEnd);

    // Draw before-match text
    if (!beforeMatch.isEmpty()) {
      QRect beforeRect = lineRect;
      beforeRect.setWidth(beforeWidth);
      p_painter->setPen(textColor);
      p_painter->drawText(beforeRect, Qt::AlignLeft | Qt::AlignVCenter, beforeMatch);
    }

    // Draw match highlight background + bold text
    QRect matchRect = lineRect;
    matchRect.setLeft(lineRect.left() + beforeWidth);
    matchRect.setWidth(matchWidth);

    // Highlight background for match
    QColor highlightBg = p_option.palette.highlight().color();
    if (!(p_option.state & QStyle::State_Selected)) {
      highlightBg.setAlpha(60);
      p_painter->fillRect(matchRect, highlightBg);
    }

    QFont boldFont = p_option.font;
    boldFont.setBold(true);
    p_painter->setFont(boldFont);
    p_painter->setPen(textColor);
    p_painter->drawText(matchRect, Qt::AlignLeft | Qt::AlignVCenter, matchText);

    // Draw after-match text
    if (!afterMatch.isEmpty()) {
      QRect afterRect = lineRect;
      afterRect.setLeft(matchRect.right());
      p_painter->setFont(p_option.font);
      p_painter->setPen(textColor);
      QString elidedAfter = fm.elidedText(afterMatch, Qt::ElideRight, afterRect.width());
      p_painter->drawText(afterRect, Qt::AlignLeft | Qt::AlignVCenter, elidedAfter);
    }
  } else {
    // No match info — draw plain text
    QString elidedText = fm.elidedText(lineText, Qt::ElideRight, lineRect.width());
    p_painter->setPen(textColor);
    p_painter->drawText(lineRect, Qt::AlignLeft | Qt::AlignVCenter, elidedText);
  }

  // Draw focus rect
  if (p_option.state & QStyle::State_HasFocus) {
    QStyleOptionFocusRect focusOpt;
    focusOpt.QStyleOption::operator=(p_option);
    focusOpt.rect = p_option.rect;
    focusOpt.state |= QStyle::State_KeyboardFocusChange;
    style->drawPrimitive(QStyle::PE_FrameFocusRect, &focusOpt, p_painter, widget);
  }

  p_painter->restore();
}
