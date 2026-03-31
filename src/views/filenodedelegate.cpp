#include "filenodedelegate.h"

#include <QApplication>
#include <QLineEdit>
#include <QPainter>
#include <QRegularExpression>
#include <QTimer>

#include <core/nodeinfo.h>
#include <core/servicelocator.h>
#include <gui/services/themeservice.h>
#include <models/notebooknodemodel.h>
#include "nodeiconhelper.h"

using namespace vnotex;

FileNodeDelegate::FileNodeDelegate(ServiceLocator &p_services, QObject *p_parent)
    : QStyledItemDelegate(p_parent), m_services(p_services) {}

FileNodeDelegate::~FileNodeDelegate() {}

void FileNodeDelegate::paint(QPainter *p_painter, const QStyleOptionViewItem &p_option,
                             const QModelIndex &p_index) const {
  if (!p_index.isValid()) {
    return;
  }

  NodeInfo nodeInfo = p_index.data(INodeListModel::NodeInfoRole).value<NodeInfo>();
  if (!nodeInfo.isValid()) {
    QStyledItemDelegate::paint(p_painter, p_option, p_index);
    return;
  }

  // Skip folders - they should use NotebookNodeDelegate
  if (nodeInfo.isFolder) {
    QStyledItemDelegate::paint(p_painter, p_option, p_index);
    return;
  }

  paintFileNode(p_painter, p_option, nodeInfo, p_index);
}

void FileNodeDelegate::paintFileNode(QPainter *p_painter, const QStyleOptionViewItem &p_option,
                                     const NodeInfo &p_nodeInfo, const QModelIndex &p_index) const {
  p_painter->save();

  // Get the widget style
  const QWidget *widget = p_option.widget;
  QStyle *style = widget ? widget->style() : QApplication::style();

  // Draw custom node background if specified (before selection)
  QColor bgColor = getNodeBackgroundColor(p_nodeInfo, p_option);
  if (bgColor.isValid()) {
    p_painter->fillRect(p_option.rect, bgColor);
  }

  // Draw selection/hover background on FULL row (respects QSS)
  style->drawPrimitive(QStyle::PE_PanelItemViewItem, &p_option, p_painter, widget);

  // --- Layout Calculations ---
  // Line 1: icon + name
  // Lines 2-3: two-line preview + tags (tags on first preview line)
  int contentTop = p_option.rect.y() + m_vPadding;
  int line1Height = m_iconSize + m_hPadding;

  // Preview font for height calculations
  QFont previewFont = p_option.font;
  previewFont.setBold(false);
  int pointSize = previewFont.pointSize();
  if (pointSize > 2) {
    previewFont.setPointSize(pointSize - 1);
  }
  QFontMetrics previewFm(previewFont);
  int previewLineHeight = previewFm.height();

  QRect line1Rect = QRect(p_option.rect.x(), contentTop, p_option.rect.width(), line1Height);
  int previewTop = contentTop + line1Height + m_lineSpacing;
  QRect previewLine1Rect = QRect(p_option.rect.x(), previewTop, p_option.rect.width(), previewLineHeight);
  QRect previewLine2Rect = QRect(p_option.rect.x(), previewTop + previewLineHeight + m_lineSpacing,
                                  p_option.rect.width(), previewLineHeight);

  // --- Line 1: Icon + Name ---
  QRect iconRect(line1Rect.x() + m_hPadding, line1Rect.y() + (line1Height - m_iconSize) / 2, m_iconSize, m_iconSize);

  QRect nameRect = line1Rect;
  nameRect.setLeft(iconRect.right() + m_hPadding);
  nameRect.setRight(p_option.rect.right() - m_hPadding);

  // Draw icon
  QIcon icon = NodeIconHelper::getNodeIcon(m_services, p_nodeInfo);
  if (!icon.isNull()) {
    QIcon::Mode iconMode = QIcon::Normal;
    if (!(p_option.state & QStyle::State_Enabled)) {
      iconMode = QIcon::Disabled;
    } else if (p_nodeInfo.isExternal) {
      // External nodes use disabled icon mode for faded appearance
      iconMode = QIcon::Disabled;
    } else if (p_option.state & QStyle::State_Selected) {
      iconMode = QIcon::Selected;
    }
    icon.paint(p_painter, iconRect, Qt::AlignCenter, iconMode);
  }

  // Determine text color
  QColor textColor = getNodeTextColor(p_nodeInfo, p_option);
  if (!textColor.isValid()) {
    textColor = p_option.palette.text().color();
  }

  // Draw name - BOLD font
  QFont boldFont = p_option.font;
  boldFont.setBold(true);
  p_painter->setFont(boldFont);
  p_painter->setPen(textColor);

  QFontMetrics boldFm(boldFont);
  QString elidedName = boldFm.elidedText(p_nodeInfo.name, Qt::ElideRight, nameRect.width());
  p_painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, elidedName);

  // --- Preview Lines (two lines, full width) ---
  QRect preview1Rect = previewLine1Rect;
  preview1Rect.setLeft(iconRect.right() + m_hPadding);
  preview1Rect.setRight(p_option.rect.right() - m_hPadding);

  QRect preview2Rect = previewLine2Rect;
  preview2Rect.setLeft(iconRect.right() + m_hPadding);
  preview2Rect.setRight(p_option.rect.right() - m_hPadding);

  // Render preview text (two lines)
  // Fetch preview via model (lazy-loaded)
  QString previewText = p_index.data(INodeListModel::PreviewRole).toString();
  if (!previewText.isEmpty()) {
    // Set up preview font and color (already calculated previewFont/previewFm above)
    p_painter->setFont(previewFont);

    // Lighter text color for preview
    QColor previewColor = textColor;
    previewColor.setAlpha(160); // ~63% opacity
    p_painter->setPen(previewColor);

    // Clean up preview text (remove newlines, collapse whitespace)
    previewText.replace(QRegularExpression("[\\r\\n\\t]+"), QStringLiteral(" "));
    previewText = previewText.simplified();

    // Calculate how many characters fit on the first line (without ellipsis)
    int availableWidth = preview1Rect.width();
    int charsOnLine1 = 0;
    int currentWidth = 0;
    for (int i = 0; i < previewText.length(); ++i) {
      int charWidth = previewFm.horizontalAdvance(previewText[i]);
      if (currentWidth + charWidth > availableWidth) {
        break;
      }
      currentWidth += charWidth;
      charsOnLine1 = i + 1;
    }

    // First preview line
    QString line1Text = previewText.left(charsOnLine1);
    if (charsOnLine1 < previewText.length()) {
      // More text remains, don't add ellipsis to line 1
      p_painter->drawText(preview1Rect, Qt::AlignLeft | Qt::AlignVCenter, line1Text);

      // Second preview line with remaining text
      QString remainingText = previewText.mid(charsOnLine1).trimmed();
      if (!remainingText.isEmpty()) {
        QString line2Text = previewFm.elidedText(remainingText, Qt::ElideRight, preview2Rect.width());
        p_painter->drawText(preview2Rect, Qt::AlignLeft | Qt::AlignVCenter, line2Text);
      }
    } else {
      // All text fits on one line
      p_painter->drawText(preview1Rect, Qt::AlignLeft | Qt::AlignVCenter, line1Text);
    }
  }

  // --- Tags Line (optional, only if tags exist) ---
  if (!p_nodeInfo.tags.isEmpty()) {
    int tagsTop = previewLine2Rect.bottom() + m_lineSpacing;
    QRect tagsRect(iconRect.right() + m_hPadding, tagsTop,
                   p_option.rect.right() - m_hPadding - (iconRect.right() + m_hPadding),
                   previewLineHeight);
    paintTags(p_painter, tagsRect, p_nodeInfo.tags, textColor);
  }
  // --- Focus Rect ---
  if (p_option.state & QStyle::State_HasFocus) {
    QStyleOptionFocusRect focusOpt;
    focusOpt.QStyleOption::operator=(p_option);
    focusOpt.rect = p_option.rect;
    focusOpt.state |= QStyle::State_KeyboardFocusChange;
    style->drawPrimitive(QStyle::PE_FrameFocusRect, &focusOpt, p_painter, widget);
  }

  p_painter->restore();
}

int FileNodeDelegate::paintTags(QPainter *p_painter, const QRect &p_rect, const QStringList &p_tags,
                                const QColor &p_textColor) const {
  if (p_tags.isEmpty()) {
    return 0;
  }

  // Save render hints and enable anti-aliasing for rounded rects
  QPainter::RenderHints oldHints = p_painter->renderHints();
  p_painter->setRenderHint(QPainter::Antialiasing, true);

  // Badge colors derived from text color
  QColor badgeBg = p_textColor;
  badgeBg.setAlpha(38); // ~15% alpha (255 * 0.15 ≈ 38)
  QColor badgeFg = p_textColor;
  badgeFg.setAlpha(200);

  // Small font for tags
  QFont tagFont = p_painter->font();
  tagFont.setBold(false);
  int pointSize = tagFont.pointSize();
  if (pointSize > 2) {
    tagFont.setPointSize(pointSize - 2);
  }
  QFontMetrics fm(tagFont);

  int tagHeight = fm.height() + 4; // +4 for padding
  int tagSpacing = 4;
  int cornerRadius = 3;

  // Calculate which tags to show
  int visibleCount = qMin(p_tags.size(), m_maxVisibleTags);
  bool hasMore = p_tags.size() > m_maxVisibleTags;

  // Build tag list for drawing (will draw right-to-left)
  QVector<QPair<QString, int>> tagsToDraw; // <text, width>

  // Add "+N" indicator if needed
  if (hasMore) {
    QString moreText = QString("+%1").arg(p_tags.size() - m_maxVisibleTags);
    int moreWidth = fm.horizontalAdvance(moreText) + 8;
    tagsToDraw.append({moreText, moreWidth});
  }

  // Add visible tags (reversed for right-to-left drawing)
  for (int i = visibleCount - 1; i >= 0; --i) {
    QString tagText = p_tags[i];
    int textWidth = fm.horizontalAdvance(tagText);
    int tagWidth = qMin(textWidth + 8, m_maxTagWidth);

    // Elide if too long
    if (textWidth + 8 > m_maxTagWidth) {
      tagText = fm.elidedText(tagText, Qt::ElideRight, m_maxTagWidth - 8);
    }
    tagsToDraw.append({tagText, tagWidth});
  }

  // Draw tags right-to-left
  int x = p_rect.right();
  int y = p_rect.top() + (p_rect.height() - tagHeight) / 2;

  p_painter->setFont(tagFont);

  for (const auto &tag : tagsToDraw) {
    int tagWidth = tag.second;
    x -= tagWidth;

    // Don't draw if it would overflow the left boundary
    if (x < p_rect.left()) {
      break;
    }

    QRect tagRect(x, y, tagWidth, tagHeight);

    // Draw rounded background
    p_painter->setPen(Qt::NoPen);
    p_painter->setBrush(badgeBg);
    p_painter->drawRoundedRect(tagRect, cornerRadius, cornerRadius);

    // Draw text
    p_painter->setPen(badgeFg);
    p_painter->drawText(tagRect, Qt::AlignCenter, tag.first);

    x -= tagSpacing;
  }

  // Restore render hints
  p_painter->setRenderHints(oldHints);

  return p_rect.right() - x; // Total width consumed
}

QSize FileNodeDelegate::sizeHint(const QStyleOptionViewItem &p_option, const QModelIndex &p_index) const {
  QFontMetrics fm(p_option.font);

  // Line 1: icon height or font height (whichever larger)
  int line1Height = qMax(fm.height(), m_iconSize) + m_hPadding;

  // Lines 2-3: two lines for preview (smaller font)
  QFont smallFont = p_option.font;
  int pointSize = smallFont.pointSize();
  if (pointSize > 2) {
    smallFont.setPointSize(pointSize - 1);
  }
  int previewLineHeight = QFontMetrics(smallFont).height();
  int previewBlockHeight = previewLineHeight * 2 + m_lineSpacing; // Two lines of preview

  // Base height = line1 + preview block + vertical padding
  int totalHeight = line1Height + m_lineSpacing + previewBlockHeight + 2 * m_vPadding;

  // Line 4 (optional): tags line - only if node has tags
  NodeInfo nodeInfo = p_index.data(INodeListModel::NodeInfoRole).value<NodeInfo>();
  if (!nodeInfo.tags.isEmpty()) {
    // Tags use smaller font
    QFont tagFont = p_option.font;
    int tagPointSize = tagFont.pointSize();
    if (tagPointSize > 2) {
      tagFont.setPointSize(tagPointSize - 2);
    }
    int tagLineHeight = QFontMetrics(tagFont).height() + 4; // +4 for badge padding
    totalHeight += m_lineSpacing + tagLineHeight;
  }

  // Ensure minimum height
  totalHeight = qMax(totalHeight, 48);

  return QSize(200, totalHeight); // Width will be determined by view
}

QColor FileNodeDelegate::getNodeBackgroundColor(const NodeInfo &p_nodeInfo,
                                                const QStyleOptionViewItem &p_option) const {
  Q_UNUSED(p_option);

  if (!p_nodeInfo.hasVisualStyle()) {
    return QColor();
  }

  QString colorStr = p_nodeInfo.backgroundColor;
  if (!colorStr.isEmpty()) {
    return QColor(colorStr);
  }

  return QColor();
}

QColor FileNodeDelegate::getNodeTextColor(const NodeInfo &p_nodeInfo,
                                          const QStyleOptionViewItem &p_option) const {
  // External nodes get a faded/semi-transparent appearance
  if (p_nodeInfo.isExternal) {
    auto *themeService = m_services.get<ThemeService>();
    if (themeService) {
      QString externalFg = themeService->paletteColor(
          QStringLiteral("widgets#notebookexplorer#external_node_text#fg"));
      if (!externalFg.isEmpty()) {
        return QColor(externalFg);
      }
    }
    // Fallback: semi-transparent gray
    return QColor(128, 128, 128, 160);
  }

  // Check for custom node visual color first
  if (p_nodeInfo.hasVisualStyle() && !p_nodeInfo.textColor.isEmpty()) {
    return QColor(p_nodeInfo.textColor);
  }

  // Get text color from theme service based on selection state
  auto *themeService = m_services.get<ThemeService>();
  QColor textColor;

  if (p_option.state & QStyle::State_Selected) {
    // Check active vs inactive window state
    if (p_option.state & QStyle::State_Active) {
      textColor =
          QColor(themeService->paletteColor(QStringLiteral("widgets#qlistview#item#selected#active#fg")));
    } else {
      textColor =
          QColor(themeService->paletteColor(QStringLiteral("widgets#qlistview#item#selected#inactive#fg")));
    }
    // Fallback to general selected color if specific one not found
    if (!textColor.isValid()) {
      textColor =
          QColor(themeService->paletteColor(QStringLiteral("widgets#qlistview#item#selected#fg")));
    }
  } else if (p_option.state & QStyle::State_MouseOver) {
    textColor = QColor(themeService->paletteColor(QStringLiteral("widgets#qlistview#item#hover#fg")));
  }

  // Fallback to normal text color
  if (!textColor.isValid()) {
    textColor = QColor(themeService->paletteColor(QStringLiteral("widgets#qlistview#fg")));
  }

  // Final fallback to palette
  if (!textColor.isValid()) {
    textColor = p_option.palette.text().color();
  }

  return textColor;
}


void FileNodeDelegate::setEditorData(QWidget *p_editor, const QModelIndex &p_index) const {
  Q_UNUSED(p_index);

  // Let the base class set the data first
  QStyledItemDelegate::setEditorData(p_editor, p_index);

  // Select only the base name (without extension)
  auto *lineEdit = qobject_cast<QLineEdit *>(p_editor);
  if (!lineEdit) {
    return;
  }

  // FileNodeDelegate is only used for files, so always try to select base name
  // Use QTimer::singleShot to defer selection until after the view's own
  // selectAll() call that happens after setEditorData returns
  QString text = lineEdit->text();
  int lastDot = text.lastIndexOf(QLatin1Char('.'));
  if (lastDot > 0) {
    QTimer::singleShot(0, lineEdit, [lineEdit, lastDot]() {
      lineEdit->setSelection(0, lastDot);
    });
  }
}
