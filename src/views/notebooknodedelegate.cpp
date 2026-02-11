#include "notebooknodedelegate.h"

#include <QApplication>
#include <QPainter>

#include <core/notebook/node.h>
#include <core/vnotex.h>
#include <models/notebooknodemodel.h>
#include <utils/iconutils.h>

using namespace vnotex;

NotebookNodeDelegate::NotebookNodeDelegate(QObject *p_parent) : QStyledItemDelegate(p_parent) {}

NotebookNodeDelegate::~NotebookNodeDelegate() {}

void NotebookNodeDelegate::paint(QPainter *p_painter, const QStyleOptionViewItem &p_option,
                                 const QModelIndex &p_index) const {
  if (!p_index.isValid()) {
    return;
  }

  // Get node from index
  Node *node = static_cast<Node *>(p_index.data(NotebookNodeModel::NodeRole).value<void *>());
  if (!node) {
    QStyledItemDelegate::paint(p_painter, p_option, p_index);
    return;
  }

  paintNode(p_painter, p_option, node);
}

void NotebookNodeDelegate::paintNode(QPainter *p_painter, const QStyleOptionViewItem &p_option,
                                     Node *p_node) const {
  p_painter->save();

  // Initialize style option
  QStyleOptionViewItem opt = p_option;
  initStyleOption(&opt, QModelIndex());

  // Get the widget style
  const QWidget *widget = opt.widget;
  QStyle *style = widget ? widget->style() : QApplication::style();

  // Draw background
  QColor bgColor = getNodeBackgroundColor(p_node, opt);
  if (bgColor.isValid()) {
    p_painter->fillRect(opt.rect, bgColor);
  }

  // Draw selection highlight (on top of custom background)
  if (opt.state & QStyle::State_Selected) {
    p_painter->fillRect(opt.rect, opt.palette.highlight());
  }

  // Calculate positions
  QRect iconRect = opt.rect;
  iconRect.setWidth(m_iconSize);
  iconRect.setLeft(iconRect.left() + m_padding);
  iconRect.setTop(iconRect.top() + (opt.rect.height() - m_iconSize) / 2);
  iconRect.setHeight(m_iconSize);

  QRect textRect = opt.rect;
  textRect.setLeft(iconRect.right() + m_padding);
  textRect.setRight(opt.rect.right() - m_padding);

  // Draw icon
  QIcon icon = getNodeIcon(p_node);
  if (!icon.isNull()) {
    QIcon::Mode iconMode = QIcon::Normal;
    if (!(opt.state & QStyle::State_Enabled)) {
      iconMode = QIcon::Disabled;
    } else if (opt.state & QStyle::State_Selected) {
      iconMode = QIcon::Selected;
    }
    icon.paint(p_painter, iconRect, Qt::AlignCenter, iconMode);
  }

  // Draw text
  QColor textColor = getNodeTextColor(p_node, opt);
  if (textColor.isValid()) {
    p_painter->setPen(textColor);
  } else if (opt.state & QStyle::State_Selected) {
    p_painter->setPen(opt.palette.highlightedText().color());
  } else {
    p_painter->setPen(opt.palette.text().color());
  }

  QString text = p_node->getName();
  QFontMetrics fm(opt.font);
  QString elidedText = fm.elidedText(text, Qt::ElideRight, textRect.width());
  style->drawItemText(p_painter, textRect, Qt::AlignLeft | Qt::AlignVCenter, opt.palette, true,
                      elidedText);

  // Draw child count badge for containers
  if (m_showChildCount && p_node->isContainer()) {
    int count = p_node->getChildrenCount();
    if (count > 0) {
      QString countText = QString("(%1)").arg(count);
      int countWidth = fm.horizontalAdvance(countText);

      QRect countRect = textRect;
      countRect.setLeft(countRect.right() - countWidth - m_padding);

      QColor countColor = opt.palette.text().color();
      countColor.setAlpha(150);
      p_painter->setPen(countColor);
      p_painter->drawText(countRect, Qt::AlignRight | Qt::AlignVCenter, countText);
    }
  }

  // Draw focus rect if focused
  if (opt.state & QStyle::State_HasFocus) {
    QStyleOptionFocusRect focusOpt;
    focusOpt.QStyleOption::operator=(opt);
    focusOpt.rect = opt.rect;
    focusOpt.state |= QStyle::State_KeyboardFocusChange;
    style->drawPrimitive(QStyle::PE_FrameFocusRect, &focusOpt, p_painter, widget);
  }

  p_painter->restore();
}

QSize NotebookNodeDelegate::sizeHint(const QStyleOptionViewItem &p_option,
                                     const QModelIndex &p_index) const {
  Q_UNUSED(p_index);

  QFontMetrics fm(p_option.font);
  int height = qMax(fm.height(), m_iconSize) + m_padding * 2;

  return QSize(200, height); // Width will be determined by view
}

void NotebookNodeDelegate::setShowChildCount(bool p_show) { m_showChildCount = p_show; }

bool NotebookNodeDelegate::showChildCount() const { return m_showChildCount; }

QColor NotebookNodeDelegate::getNodeBackgroundColor(Node *p_node,
                                                    const QStyleOptionViewItem &p_option) const {
  Q_UNUSED(p_option);

  if (!p_node) {
    return QColor();
  }

  QString colorStr = p_node->getEffectiveBackgroundColor();
  if (!colorStr.isEmpty()) {
    return QColor(colorStr);
  }

  return QColor();
}

QColor NotebookNodeDelegate::getNodeTextColor(Node *p_node,
                                              const QStyleOptionViewItem &p_option) const {
  Q_UNUSED(p_option);

  if (!p_node) {
    return QColor();
  }

  QString colorStr = p_node->getNameColor();
  if (!colorStr.isEmpty()) {
    return QColor(colorStr);
  }

  return QColor();
}

QIcon NotebookNodeDelegate::getNodeIcon(Node *p_node) const {
  if (!p_node) {
    return QIcon();
  }

  const auto &themeMgr = VNoteX::getInst().getThemeMgr();
  QString iconName;

  if (p_node->isContainer()) {
    iconName = QStringLiteral("folder_node.svg");
  } else if (p_node->hasContent()) {
    iconName = QStringLiteral("file_node.svg");
  } else {
    iconName = QStringLiteral("file_node.svg");
  }

  return IconUtils::fetchIcon(themeMgr.getIconFile(iconName));
}
