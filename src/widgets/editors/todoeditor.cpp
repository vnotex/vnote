#include "todoeditor.h"

#include <QActionGroup>
#include <QApplication>
#include <QCalendarWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDrag>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QInputMethodEvent>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOption>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>
#include <functional>

#include <controllers/todoviewwindowcontroller.h>
#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/servicelocator.h>
#include <gui/services/themeservice.h>
#include <gui/utils/iconutils.h>
#include <gui/utils/treechildcountdelegate.h>
#include <gui/utils/widgetutils.h>
#include <m3/qt/emoji_line_edit.h>
#include <models/todomodels.h>
#include <vtextedit/vtextedit.h>
#include <widgets/plaintextedit.h>

#include "simplemarkdowneditor.h"
#include "todotaskview.h"

using namespace vnotex;

namespace {
template <typename T> T *named(T *p_widget, const char *p_name, const QString &p_accessibleName) {
  p_widget->setObjectName(QString::fromLatin1(p_name));
  p_widget->setAccessibleName(p_accessibleName);
  return p_widget;
}

void spacing(QLayout *p_layout) {
  p_layout->setContentsMargins(8, 8, 8, 8);
  p_layout->setSpacing(4);
}

QString groupName(const TodoGroup &p_group) {
  return p_group.m_title.trimmed().isEmpty() ? TodoEditor::tr("Untitled group") : p_group.m_title;
}

QString listName(const TodoList &p_list) {
  return p_list.m_title.trimmed().isEmpty() ? TodoEditor::tr("Untitled list") : p_list.m_title;
}

QString qualifiedListName(const TodoDocument &p_document, const TodoList &p_list) {
  const auto *group = p_document.groupForList(p_list);
  return group && !group->m_isDefault
             ? TodoEditor::tr("%1 / %2").arg(groupName(*group), listName(p_list))
             : listName(p_list);
}

void addGroupChoices(QComboBox *p_combo, const TodoDocument &p_document) {
  p_combo->addItem(TodoEditor::tr("No group"), QString());
  for (const auto &group : p_document.m_groups) {
    if (!group.m_isDefault) {
      p_combo->addItem(groupName(group), group.m_key);
    }
  }
}

QString taskName(const TodoItem &p_item) {
  return p_item.m_fields.m_title.trimmed().isEmpty() ? TodoEditor::tr("Untitled task")
                                                     : p_item.m_fields.m_title;
}

class TodoListView final : public QListView {
public:
  using QListView::QListView;

  void doItemsLayout() override {
    QListView::doItemsLayout();
    updateGroupCursor();
  }

protected:
  void mousePressEvent(QMouseEvent *p_event) override {
    m_suppressGroupRelease = false;
    m_groupPress = p_event->button() == Qt::LeftButton &&
                   indexAt(p_event->position().toPoint()).data(TodoListModel::GroupRole).toBool();
    QListView::mousePressEvent(p_event);
  }

  void mouseDoubleClickEvent(QMouseEvent *p_event) override {
    if (p_event->button() == Qt::LeftButton && m_groupPress) {
      // The first click already toggled the group. Consume the second click and release.
      m_suppressGroupRelease = true;
      p_event->accept();
      return;
    }
    QListView::mouseDoubleClickEvent(p_event);
  }

  void mouseReleaseEvent(QMouseEvent *p_event) override {
    if (p_event->button() == Qt::LeftButton && m_suppressGroupRelease) {
      m_suppressGroupRelease = false;
      p_event->accept();
      return;
    }
    QListView::mouseReleaseEvent(p_event);
  }

  void mouseMoveEvent(QMouseEvent *p_event) override {
    setGroupCursor(indexAt(p_event->position().toPoint()));
    QListView::mouseMoveEvent(p_event);
  }

  bool viewportEvent(QEvent *p_event) override {
    const QPointer<TodoListView> self(this);
    const bool handled = QListView::viewportEvent(p_event);
    if (self) {
      if (p_event->type() == QEvent::Enter) {
        setGroupCursor(indexAt(static_cast<QEnterEvent *>(p_event)->position().toPoint()));
      } else if (p_event->type() == QEvent::Leave) {
        setGroupCursor({});
      }
    }
    return handled;
  }

  void scrollContentsBy(int p_dx, int p_dy) override {
    QListView::scrollContentsBy(p_dx, p_dy);
    updateGroupCursor();
  }

  void changeEvent(QEvent *p_event) override {
    QListView::changeEvent(p_event);
    if (p_event->type() == QEvent::EnabledChange) {
      updateGroupCursor();
    }
  }

  void startDrag(Qt::DropActions p_supportedActions) override {
    if (!(p_supportedActions & Qt::MoveAction) || !model()) {
      return;
    }
    const auto indexes = selectedIndexes();
    auto *mime = model()->mimeData(indexes);
    if (!mime) {
      return;
    }
    QPointer<QDrag> drag = new QDrag(this);
    drag->setMimeData(mime);
    if (!indexes.isEmpty()) {
      const auto rect = visualRect(indexes.first()).intersected(viewport()->rect());
      if (!rect.isEmpty()) {
        drag->setPixmap(viewport()->grab(rect));
      }
    }
    drag->exec(Qt::MoveAction, Qt::MoveAction);
    // The controller already moved the source. Never let the view remove it again.
    if (drag) {
      drag->deleteLater();
    }
  }

  void dragMoveEvent(QDragMoveEvent *p_event) override {
    setDropIndicatorShown(true);
    QAbstractItemView::dragMoveEvent(p_event);
    m_groupDropRow = groupDropRow(p_event);
    if (m_groupDropRow >= 0) {
      p_event->acceptProposedAction();
    }
    // Qt retains its previous indicator when a subsequent target is rejected.
    setDropIndicatorShown(m_groupDropRow < 0 && p_event->isAccepted());
    viewport()->update();
  }

  void dragLeaveEvent(QDragLeaveEvent *p_event) override {
    m_groupDropRow = -1;
    QAbstractItemView::dragLeaveEvent(p_event);
    setDropIndicatorShown(true);
  }

  void paintEvent(QPaintEvent *p_event) override {
    QListView::paintEvent(p_event);
    if (m_groupDropRow < 0 || !model()) {
      return;
    }
    int y = 0;
    if (m_groupDropRow < model()->rowCount()) {
      y = visualRect(model()->index(m_groupDropRow, 0)).top();
    } else {
      int last = model()->rowCount() - 1;
      while (last >= 0 && isRowHidden(last)) {
        --last;
      }
      y = visualRect(model()->index(last, 0)).bottom();
    }
    QStyleOption option;
    option.initFrom(this);
    option.rect = QRect(0, y, viewport()->width() - 1, 0);
    QPainter painter(viewport());
    style()->drawPrimitive(QStyle::PE_IndicatorItemViewItemDrop, &option, &painter, this);
  }

  void dropEvent(QDropEvent *p_event) override {
    const int row = groupDropRow(p_event);
    m_groupDropRow = -1;
    if (row >= 0 && model()->dropMimeData(p_event->mimeData(), p_event->dropAction(), row, 0, {})) {
      // Accepted events make the base clean up without attempting a second source move.
      p_event->acceptProposedAction();
    }
    // QListView optimizes adjacent/end row moves before consulting the model.
    // Sidebar viewport drops can change ownership even for the final row.
    QAbstractItemView::dropEvent(p_event);
    setDropIndicatorShown(true);
  }

private:
  int groupDropRow(const QDropEvent *p_event) const {
    const auto position = p_event->position().toPoint();
    const auto index = indexAt(position);
    if (!index.data(TodoListModel::GroupRole).toBool() ||
        model()->canDropMimeData(p_event->mimeData(), p_event->dropAction(), -1, 0, index)) {
      // Lists already drop on headers to join groups; only group drags need a root gap.
      return -1;
    }
    int row = index.row();
    if (position.y() > visualRect(index).center().y()) {
      const auto groupKey = index.data(TodoListModel::GroupIdRole);
      do {
        ++row;
      } while (row < model()->rowCount() &&
               model()->index(row, 0).data(TodoListModel::GroupIdRole) == groupKey);
    }
    return model()->canDropMimeData(p_event->mimeData(), p_event->dropAction(), row, 0, {}) ? row
                                                                                            : -1;
  }

  void updateGroupCursor() {
    const auto *port = viewport();
    setGroupCursor(port->underMouse() ? indexAt(port->mapFromGlobal(QCursor::pos()))
                                      : QModelIndex());
  }

  void setGroupCursor(const QModelIndex &p_index) {
    auto *port = viewport();
    if (isEnabled() && p_index.data(TodoListModel::GroupRole).toBool()) {
      if (port->cursor().shape() != Qt::PointingHandCursor) {
        port->setCursor(Qt::PointingHandCursor);
      }
    } else if (port->testAttribute(Qt::WA_SetCursor)) {
      port->unsetCursor();
    }
  }

  int m_groupDropRow = -1;
  bool m_groupPress = false;
  bool m_suppressGroupRelease = false;
};

class ListDelegate : public TreeChildCountDelegate {
public:
  ListDelegate(ThemeService *p_theme, QListView *p_view) : TreeChildCountDelegate(p_view) {
    // Reusable 24px outline glyphs, scaled to the current font in paint().
    auto &list = m_icons[static_cast<int>(TodoViewKind::List)];
    for (int y : {5, 12, 19}) {
      list.moveTo(2, y);
      list.lineTo(22, y);
    }
    auto &sun = m_icons[static_cast<int>(TodoViewKind::MyDay)];
    sun.addEllipse(QRectF(7, 7, 10, 10));
    constexpr double pi = 3.14159265358979323846;
    for (int i = 0; i < 8; ++i) {
      const double angle = i * pi / 4;
      sun.moveTo(12 + 9 * std::cos(angle), 12 + 9 * std::sin(angle));
      sun.lineTo(12 + 12 * std::cos(angle), 12 + 12 * std::sin(angle));
    }
    auto &star = m_icons[static_cast<int>(TodoViewKind::Important)];
    for (int i = 0; i < 10; ++i) {
      const double angle = -pi / 2 + i * pi / 5;
      const double radius = i % 2 ? 5 : 11;
      const QPointF point(12 + std::cos(angle) * radius, 12 + std::sin(angle) * radius);
      if (i == 0) {
        star.moveTo(point);
      } else {
        star.lineTo(point);
      }
    }
    star.closeSubpath();
    auto &planned = m_icons[static_cast<int>(TodoViewKind::Planned)];
    planned.addRoundedRect(QRectF(3, 3, 18, 18), 2, 2);
    for (int x : {7, 12, 17}) {
      planned.moveTo(x, 7);
      planned.lineTo(x, 17);
    }
    auto &home = m_icons[static_cast<int>(TodoViewKind::All)];
    home.moveTo(3, 11);
    home.lineTo(12, 2);
    home.lineTo(21, 11);
    home.lineTo(21, 22);
    home.lineTo(15, 22);
    home.lineTo(15, 15);
    home.lineTo(9, 15);
    home.lineTo(9, 22);
    home.lineTo(3, 22);
    home.closeSubpath();
    m_groupIcon.addRoundedRect(QRectF(3, 3, 18, 18), 2, 2);
    m_groupIcon.moveTo(8, 7);
    m_groupIcon.lineTo(8, 17);
    const auto refreshColors = [this, p_theme, p_view]() {
      m_foreground = QColor(p_theme->paletteColor(QStringLiteral("widgets#qlistview#fg")));
      m_activeForeground = QColor(
          p_theme->paletteColor(QStringLiteral("widgets#qlistview#item#selected#active#fg")));
      m_activeBackground = QColor(
          p_theme->paletteColor(QStringLiteral("widgets#qlistview#item#selected#active#bg")));
      m_disabled = QColor(p_theme->paletteColor(QStringLiteral("base#disabled#fg")));
      m_accent = QColor(p_theme->paletteColor(QStringLiteral("base#master#bg")));
      m_border = QColor(p_theme->paletteColor(QStringLiteral("base#normal#border")));
      m_iconColors[static_cast<int>(TodoViewKind::List)] =
          QColor(p_theme->paletteColor(QStringLiteral("base#info#fg")));
      m_iconColors[static_cast<int>(TodoViewKind::MyDay)] =
          QColor(p_theme->paletteColor(QStringLiteral("base#warning#fg")));
      m_iconColors[static_cast<int>(TodoViewKind::Important)] =
          QColor(p_theme->paletteColor(QStringLiteral("base#error#fg")));
      m_iconColors[static_cast<int>(TodoViewKind::Planned)] =
          QColor(p_theme->paletteColor(QStringLiteral("base#success#fg")));
      m_iconColors[static_cast<int>(TodoViewKind::All)] = m_foreground;
      p_view->viewport()->update();
    };
    refreshColors();
    connect(p_theme, &ThemeService::themeChanged, this, refreshColors);
  }

  void paint(QPainter *p_painter, const QStyleOptionViewItem &p_option,
             const QModelIndex &p_index) const override {
    QStyleOptionViewItem option(p_option);
    initStyleOption(&option, p_index);
    const bool group = p_index.data(TodoListModel::GroupRole).toBool();
    const auto view = p_index.data(TodoListModel::ViewKindRole).value<TodoViewKind>();
    const bool separator = !group && view == TodoViewKind::All;
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    const bool enabled = option.state & QStyle::State_Enabled;
    // Navigation keeps its selected color pair when focus moves to the tasks.
    const QColor foreground = !enabled ? m_disabled : selected ? m_activeForeground : m_foreground;
    QRect row = option.rect.adjusted(4, 2, -4, separator ? -18 : -2);
    const auto visual = [&](const QRect &p_rect) {
      return QStyle::visualRect(option.direction, row, p_rect);
    };
    p_painter->save();
    p_painter->setClipRect(option.rect);
    p_painter->setRenderHint(QPainter::Antialiasing);
    p_painter->setPen(Qt::NoPen);
    if (selected || hovered) {
      auto hoverBackground = m_foreground;
      hoverBackground.setAlpha(18);
      p_painter->setBrush(selected ? m_activeBackground : hoverBackground);
      p_painter->drawRoundedRect(row, 4, 4);
    }
    const int side = qMax(20, option.fontMetrics.height());
    const int indent =
        !group && !p_index.data(TodoListModel::GroupIdRole).toString().isEmpty() ? side + 8 : 0;
    if (selected) {
      p_painter->setBrush(qAbs(m_accent.lightnessF() - m_activeBackground.lightnessF()) < 0.15
                              ? foreground
                              : m_accent);
      p_painter->drawRoundedRect(
          visual(QRect(row.left() + indent + 4, row.center().y() - 9, 3, 18)), 1.5, 1.5);
    }
    const QRect icon =
        visual(QRect(row.left() + indent + 16, row.center().y() - side / 2, side, side));
    const auto glyph = p_index.data(TodoListModel::IconRole).toString();
    if (!glyph.isEmpty()) {
      QFont glyphFont = option.font;
      glyphFont.setPixelSize(side);
      p_painter->setFont(glyphFont);
      p_painter->setPen(foreground);
      // Emoji fallback fonts can extend beyond the em square; keep only the row clip.
      p_painter->drawText(icon, Qt::AlignCenter | Qt::TextDontClip, glyph);
    } else {
      p_painter->save();
      p_painter->translate(icon.topLeft());
      p_painter->scale(side / 24.0, side / 24.0);
      p_painter->setBrush(Qt::NoBrush);
      p_painter->setPen(
          QPen(selected || !enabled || group ? foreground : m_iconColors[static_cast<int>(view)],
               1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      p_painter->drawPath(group ? m_groupIcon : m_icons[static_cast<int>(view)]);
      p_painter->restore();
    }

    int trailing = row.right() - 12;
    ChildCountBadge groupBadge;
    p_painter->setFont(option.font);
    if (group) {
      const QRect chevron = visual(QRect(trailing - 12, row.center().y() - 6, 12, 12));
      const bool expanded = p_index.data(TodoListModel::ExpandedRole).toBool();
      const int edgeY = chevron.center().y() + (expanded ? 2 : -2);
      const int middleY = chevron.center().y() + (expanded ? -3 : 3);
      p_painter->setPen(QPen(foreground, 1.3, Qt::SolidLine, Qt::RoundCap));
      p_painter->drawLine(chevron.left(), edgeY, chevron.center().x(), middleY);
      p_painter->drawLine(chevron.center().x(), middleY, chevron.right(), edgeY);
      trailing -= 24;
      groupBadge = childCountBadge(option, p_index.data(TodoListModel::ListCountRole).toInt());
      if (!groupBadge.m_rect.isEmpty()) {
        groupBadge.m_rect.moveTopRight(
            QPoint(trailing - 1, row.center().y() - groupBadge.m_rect.height() / 2));
        trailing = groupBadge.m_rect.left() - groupBadge.m_gap;
        groupBadge.m_rect = visual(groupBadge.m_rect);
        option.palette.setColor(QPalette::Text, m_foreground);
        option.palette.setColor(QPalette::Disabled, QPalette::Text, m_disabled);
      }
    } else {
      const int count = p_index.data(TodoListModel::IncompleteCountRole).toInt();
      if (count > 0) {
        const auto text = QString::number(count);
        const int height = option.fontMetrics.height() + 4;
        const int width = qMax(height, option.fontMetrics.horizontalAdvance(text) + 10);
        const QRect badge =
            visual(QRect(trailing - width, row.center().y() - height / 2, width, height));
        auto background = foreground;
        background.setAlpha(24);
        p_painter->setPen(Qt::NoPen);
        p_painter->setBrush(background);
        p_painter->drawRoundedRect(badge, height / 2.0, height / 2.0);
        p_painter->setPen(foreground);
        p_painter->drawText(badge, Qt::AlignCenter, text);
        trailing -= width + 10;
      }
    }
    const int textLeft = row.left() + indent + 16 + side + 14;
    const QRect textRect =
        visual(QRect(textLeft, row.top(), qMax(0, trailing - textLeft), row.height()));
    p_painter->setPen(foreground);
    p_painter->drawText(
        textRect, Qt::AlignVCenter | Qt::AlignLeading,
        option.fontMetrics.elidedText(option.text, Qt::ElideRight, textRect.width()));
    if (option.state & QStyle::State_HasFocus) {
      p_painter->setPen(QPen(foreground, 1, Qt::DotLine));
      p_painter->setBrush(Qt::NoBrush);
      p_painter->drawRoundedRect(row.adjusted(1, 1, -1, -1), 4, 4);
    }
    paintChildCount(p_painter, groupBadge, option);
    if (separator) {
      p_painter->setPen(m_border);
      const int y = option.rect.bottom() - 7;
      p_painter->drawLine(option.rect.left() + 4, y, option.rect.right() - 4, y);
    }
    p_painter->restore();
  }

  QSize sizeHint(const QStyleOptionViewItem &p_option, const QModelIndex &p_index) const override {
    const bool separator =
        !p_index.data(TodoListModel::GroupRole).toBool() &&
        p_index.data(TodoListModel::ViewKindRole).value<TodoViewKind>() == TodoViewKind::All;
    return QSize(0, qMax(44, p_option.fontMetrics.height() + 24) + (separator ? 16 : 0));
  }

private:
  QPainterPath m_icons[5];
  QPainterPath m_groupIcon;
  QColor m_iconColors[5];
  QColor m_foreground;
  QColor m_activeForeground;
  QColor m_activeBackground;
  QColor m_disabled;
  QColor m_accent;
  QColor m_border;
};

// Circular presentation, with independent checkbox interaction and accessibility.
class CompletionCheckBox final : public QCheckBox {
public:
  CompletionCheckBox(ThemeService *p_theme, QWidget *p_parent)
      : QCheckBox(p_parent), m_theme(p_theme) {
    Q_ASSERT(m_theme);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(m_theme, &ThemeService::themeChanged, this, [this]() { update(); });
  }

  QSize sizeHint() const override {
    const int side = qMax(20, fontMetrics().height()) + 8;
    return QSize(side, side);
  }

  QSize minimumSizeHint() const override { return sizeHint(); }

protected:
  bool hitButton(const QPoint &p_position) const override { return rect().contains(p_position); }

  void paintEvent(QPaintEvent *) override {
    // Do not call CE_CheckBox: theme stylesheets supply a square indicator image.
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const int side = qMax(20, fontMetrics().height());
    const QRect circle((width() - side) / 2, (height() - side) / 2, side, side);
    const auto group = isEnabled() ? QPalette::Active : QPalette::Disabled;
    const QColor foreground =
        isEnabled()
            ? QColor(m_theme->paletteColor(underMouse()
                                               ? QStringLiteral("widgets#qlistview#item#hover#fg")
                                               : QStringLiteral("widgets#qlistview#fg")))
            : palette().color(group, QPalette::Text);
    paintTodoCompletionIndicator(&painter, circle, isChecked(), foreground);
    if (hasFocus()) {
      painter.setPen(QPen(QColor(m_theme->paletteColor(QStringLiteral("base#master#bg"))), 1.5));
      painter.setBrush(Qt::NoBrush);
      painter.drawEllipse(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5));
    }
  }

  void enterEvent(QEnterEvent *p_event) override {
    QCheckBox::enterEvent(p_event);
    update();
  }

  void leaveEvent(QEvent *p_event) override {
    QCheckBox::leaveEvent(p_event);
    update();
  }

private:
  ThemeService *m_theme;
};

} // namespace

TodoEditor::TodoEditor(ServiceLocator &p_services, QWidget *p_parent)
    : QWidget(p_parent), m_services(p_services),
      m_paneLayout(p_services.get<ConfigMgr2>()->getSessionConfig().getTodoEditorLayout()) {
  named(this, "todoEditor", tr("TODO planner"));
  createUi();
  connect(m_services.get<ConfigMgr2>(), &ConfigMgr2::mainConfigChanged, this,
          &TodoEditor::applySortOrder);
  qApp->installEventFilter(this);
  refresh();
}

TodoEditor::~TodoEditor() { qApp->removeEventFilter(this); }

void TodoEditor::createUi() {
  auto *root = new QVBoxLayout(this);
  spacing(root);
  root->setContentsMargins(4, 4, 4, 4);
  m_navigation = named(new QWidget(this), "todoNarrowNavigation", tr("List navigation"));
  auto *navigation = new QHBoxLayout(m_navigation);
  navigation->setContentsMargins(8, 0, 8, 0);
  m_listsButton =
      named(new QPushButton(m_navigation), "todoListsButton", tr("Show lists and search"));
  m_listsButton->setIconSize(QSize(20, 20));
  m_listsButton->setFlat(true);
  m_listsButton->setStyleSheet(QStringLiteral("QPushButton { min-width: 0; padding: 6px; }"));
  m_listsButton->setCheckable(true);
  m_listsButton->setToolTip(tr("Show lists and search"));
  navigation->addWidget(m_listsButton);
  navigation->addStretch();
  connect(m_listsButton, &QPushButton::clicked, this, &TodoEditor::setSidebarOpen);
  root->addWidget(m_navigation);

  m_splitter = named(new QSplitter(Qt::Horizontal, this), "todoSplitter", tr("Planner panes"));
  m_splitter->setChildrenCollapsible(false);
  m_splitter->setHandleWidth(4);
  root->addWidget(m_splitter, 1);
  m_sidebar = named(new QWidget(m_splitter), "todoSidebar", tr("Lists and search"));
  m_sidebar->setAutoFillBackground(true);
  auto *sidebar = new QVBoxLayout(m_sidebar);
  spacing(sidebar);
  auto *searchWidget = new QWidget(m_sidebar);
  auto *searchArea = new QVBoxLayout(searchWidget);
  searchArea->setContentsMargins(8, 4, 8, 4);
  searchArea->setSpacing(4);
  m_search = named(new QLineEdit(searchWidget), "todoSearch", tr("Search tasks in this file"));
  m_search->setPlaceholderText(tr("Search tasks"));
  m_search->setClearButtonEnabled(true);
  auto *searchAction = m_search->addAction(QIcon(), QLineEdit::TrailingPosition);
  searchAction->setText(tr("Search tasks"));
  connect(searchAction, &QAction::triggered, this, &TodoEditor::focusSearch);
  connect(m_search, &QLineEdit::textChanged, searchAction,
          [searchAction](const QString &p_text) { searchAction->setVisible(p_text.isEmpty()); });
  searchArea->addWidget(m_search);
  sidebar->addWidget(searchWidget);
  auto *theme = m_services.get<ThemeService>();
  Q_ASSERT(theme);
  m_lists = named(new TodoListView(m_sidebar), "todoLists", tr("Lists and smart views"));
  m_lists->setItemDelegate(new ListDelegate(theme, m_lists));
  m_lists->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_lists->setSelectionMode(QAbstractItemView::SingleSelection);
  // Same-model MIME admission is owned by TodoListModel, not Qt's row-move policy.
  m_lists->setDragDropMode(QAbstractItemView::DragDrop);
  m_lists->setDefaultDropAction(Qt::MoveAction);
  m_lists->setDragDropOverwriteMode(false);
  m_lists->setDropIndicatorShown(true);
  m_lists->setContextMenuPolicy(Qt::CustomContextMenu);
  m_lists->setMouseTracking(true);
  m_lists->setFrameShape(QFrame::NoFrame);
  m_lists->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_lists->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  sidebar->addWidget(m_lists, 1);
  auto *creationWidget = named(new QWidget(m_sidebar), "todoCreationArea", tr("Lists actions"));
  auto *creation = new QHBoxLayout(creationWidget);
  creation->setContentsMargins(8, 4, 8, 4);
  creation->setSpacing(4);
  m_newList = named(new QPushButton(tr("New List"), creationWidget), "todoNewList", tr("New list"));
  m_newGroup = named(new QPushButton(creationWidget), "todoNewGroup", tr("New group"));
  m_newGroup->setToolTip(tr("New group"));
  m_newGroup->setFixedWidth(40);
  m_archiveCompleted =
      named(new QPushButton(creationWidget), "todoArchiveCompleted", tr("Archive Completed Tasks"));
  m_archiveCompleted->setFixedWidth(40);
  for (auto *button : {m_newList, m_newGroup, m_archiveCompleted}) {
    button->setFlat(true);
    button->setIconSize(QSize(20, 20));
    button->setStyleSheet(
        QStringLiteral("QPushButton { min-width: 0; text-align: left; padding: 8px; border: "
                       "1px solid transparent; background: transparent; }"
                       "QPushButton:hover { background: palette(midlight); }"
                       "QPushButton:focus { border-color: palette(mid); }"));
  }
  creation->addWidget(m_newList, 1);
  creation->addWidget(m_newGroup);
  creation->addWidget(m_archiveCompleted);
  sidebar->addWidget(creationWidget);
  const auto refreshIcons = [this, theme, searchAction]() {
    searchAction->setIcon(IconUtils::fetchIcon(theme->getIconFile(QStringLiteral("search.svg"))));
    m_newList->setIcon(
        IconUtils::fetchIconWithDisabledState(theme->getIconFile(QStringLiteral("add.svg"))));
    m_newGroup->setIcon(
        IconUtils::fetchIconWithDisabledState(theme->getIconFile(QStringLiteral("new_group.svg"))));
    m_archiveCompleted->setIcon(IconUtils::fetchIconWithDisabledState(
        theme->getIconFile(QStringLiteral("file_archive.svg"))));
    m_listsButton->setIcon(
        IconUtils::fetchIconWithDisabledState(theme->getIconFile(QStringLiteral("list_tree.svg"))));
    m_listMore->setIcon(
        IconUtils::fetchIconWithDisabledState(theme->getIconFile(QStringLiteral("menu.svg"))));
    refreshListIcon();
    refreshHeadingFont();
  };

  m_pages = named(new QStackedWidget(m_splitter), "todoNativePages", tr("Tasks or details"));
  m_taskPane = named(new QWidget(m_pages), "todoTaskPane", tr("Tasks"));
  m_pages->addWidget(m_taskPane);
  auto *tasksLayout = new QVBoxLayout(m_taskPane);
  tasksLayout->setContentsMargins(16, 16, 16, 12);
  tasksLayout->setSpacing(12);
  auto *heading = new QHBoxLayout;
  heading->setContentsMargins(0, 4, 0, 4);
  heading->setSpacing(8);
  m_listIcon = named(new QPushButton(m_taskPane), "todoListIcon", tr("Add list icon"));
  m_listIcon->setIconSize(QSize(24, 24));
  m_listIcon->setFlat(true);
  m_listIcon->setStyleSheet(QStringLiteral("QPushButton { min-width: 0; padding: 8px; }"));
  auto iconFont = m_listIcon->font();
  iconFont.setPixelSize(24);
  m_listIcon->setFont(iconFont);
  heading->addWidget(m_listIcon, 0, Qt::AlignVCenter);
  m_listTitle = named(new QLineEdit(m_taskPane), "todoListTitle", tr("List title"));
  m_listTitle->setPlaceholderText(tr("Untitled list"));
  m_listTitle->setStyleSheet(QStringLiteral("QLineEdit { padding: 6px 8px; min-height: 32px; }"));
  m_viewTitle = named(new QLabel(m_taskPane), "todoViewTitle", tr("Current view"));
  heading->addWidget(m_listTitle, 1);
  heading->addWidget(m_viewTitle, 1);
  m_listMore = named(new QPushButton(m_taskPane), "todoListMore", tr("List Options"));
  m_listMore->setToolTip(tr("List Options"));
  m_listMore->setIconSize(QSize(24, 24));
  m_listMore->setFlat(true);
  m_listMore->setStyleSheet(QStringLiteral("QPushButton { min-width: 0; padding: 8px; }"));
  refreshIcons();
  connect(theme, &ThemeService::themeChanged, this, refreshIcons);
  heading->addWidget(m_listMore, 0, Qt::AlignVCenter);
  tasksLayout->addLayout(heading);
  m_description = named(new QLabel(m_taskPane), "todoListDescription", tr("List description"));
  m_description->setTextFormat(Qt::PlainText);
  m_description->setWordWrap(true);
  m_description->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  m_description->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  m_description->setMinimumWidth(0);
  m_description->setMaximumHeight(104);
  tasksLayout->addWidget(m_description);
  m_tasks = named(new TodoTaskView(m_services, m_taskPane), "todoTasks",
                  tr("Tasks; Enter opens details, Space completes"));
  m_tasks->setDragDropMode(QAbstractItemView::InternalMove);
  m_tasks->setDefaultDropAction(Qt::MoveAction);
  m_tasks->setDragDropOverwriteMode(false);
  m_tasks->setDropIndicatorShown(true);
  m_tasks->setContextMenuPolicy(Qt::CustomContextMenu);
  tasksLayout->addWidget(m_tasks, 1);
  m_destination =
      named(new QComboBox(m_taskPane), "todoDestination", tr("New task destination list"));
  m_destination->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_destination->setMinimumContentsLength(8);
  tasksLayout->addWidget(m_destination);
  auto *addDate = new QHBoxLayout;
  addDate->setSpacing(4);
  m_addHasDue = named(new QCheckBox(tr("Due date"), m_taskPane), "todoAddHasDue",
                      tr("Choose a due date for the new task"));
  m_addDue =
      named(new QDateEdit(QDate::currentDate(), m_taskPane), "todoAddDue", tr("New task due date"));
  m_addDue->setCalendarPopup(true);
  m_addDue->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
  addDate->addWidget(m_addHasDue);
  addDate->addWidget(m_addDue);
  addDate->addStretch();
  tasksLayout->addLayout(addDate);
  auto *add = new QHBoxLayout;
  add->setSpacing(8);
  m_addTask = named(new QLineEdit(m_taskPane), "todoAddTask", tr("Add a task"));
  m_addTask->setPlaceholderText(tr("Add a task"));
  m_addTask->setStyleSheet(QStringLiteral("QLineEdit { padding: 10px 12px; min-height: 20px; }"));
  m_addButton = named(new QPushButton(tr("Add"), m_taskPane), "todoAddButton", tr("Add task"));
  m_addButton->setStyleSheet(
      QStringLiteral("QPushButton { padding: 10px 16px; min-height: 20px; min-width: 0; }"));
  add->addWidget(m_addTask, 1);
  add->addWidget(m_addButton);
  tasksLayout->addLayout(add);

  createDetails();
  m_splitter->setStretchFactor(0, 0);
  m_splitter->setStretchFactor(1, 1);
  m_splitter->setStretchFactor(2, 0);
  m_splitter->setSizes({m_paneLayout.m_listsWidth, 480, m_paneLayout.m_detailsWidth});
  connect(m_splitter, &QSplitter::splitterMoved, this, [this]() {
    if (m_narrow) {
      return;
    }
    const auto sizes = m_splitter->sizes();
    m_paneLayout.m_listsWidth = sizes[0];
    // A hidden pane has zero geometry, not a new preferred width.
    if (!m_detailsScroll->isHidden()) {
      m_paneLayout.m_detailsWidth = sizes[2];
    }
    m_services.get<ConfigMgr2>()->getSessionConfig().setTodoEditorLayout(m_paneLayout);
  });
  m_navigation->hide();

  connect(m_search, &QLineEdit::textChanged, this, [this](const QString &p_text) {
    if (!m_updating && m_controller) {
      endTyping();
      m_controller->setSearch(p_text);
    }
  });
  connect(m_newList, &QPushButton::clicked, this, [this]() { addList(); });
  connect(m_newGroup, &QPushButton::clicked, this, &TodoEditor::addGroup);
  connect(m_archiveCompleted, &QPushButton::clicked, this, &TodoEditor::archiveCompletedTasks);
  connect(m_lists, &QListView::clicked, this, [this](const QModelIndex &p_index) {
    if (p_index.data(TodoListModel::GroupRole).toBool()) {
      toggleGroup(p_index.data(TodoListModel::GroupIdRole).toString());
    } else {
      selectView(p_index.row());
    }
  });
  connect(m_lists, &QListView::activated, this, [this](const QModelIndex &p_index) {
    if (!p_index.data(TodoListModel::GroupRole).toBool()) {
      selectView(p_index.row());
    }
  });
  connect(m_lists, &QWidget::customContextMenuRequested, this, [this](const QPoint &p_position) {
    auto index = m_lists->indexAt(p_position);
    if (!index.isValid()) {
      index = m_lists->currentIndex();
    }
    if (index.data(TodoListModel::GroupRole).toBool()) {
      showGroupMenu(index.data(TodoListModel::GroupIdRole).toString(),
                    m_lists->viewport()->mapToGlobal(p_position));
    } else {
      showListMenu(index.data(TodoListModel::IdRole).toString(),
                   m_lists->viewport()->mapToGlobal(p_position));
    }
  });
  connect(m_tasks, &TodoTaskView::taskClicked, this, [this](const QModelIndex &p_index) {
    const bool close = m_closeDetailsOnTaskClick;
    m_closeDetailsOnTaskClick = false;
    if (close && m_detailsOpen && p_index.data(TodoTaskModel::IdRole).toString() == m_detailsKey) {
      closeDetails();
    } else {
      openTask(p_index);
    }
  });
  connect(m_tasks, &QWidget::customContextMenuRequested, this, [this](const QPoint &p_position) {
    auto index = m_tasks->indexAt(p_position);
    if (!index.isValid()) {
      index = m_tasks->currentIndex();
    }
    showTaskMenu(index.data(TodoTaskModel::IdRole).toString(),
                 m_tasks->viewport()->mapToGlobal(p_position));
  });
  connect(m_listIcon, &QPushButton::clicked, this, &TodoEditor::chooseListIcon);
  connect(m_listMore, &QPushButton::clicked, this, [this]() {
    if (m_controller) {
      const auto &projection = m_controller->getProjection();
      const auto key = projection.m_view == TodoViewKind::List && projection.m_search.isEmpty() &&
                               projection.m_tag.isEmpty()
                           ? projection.m_listKey
                           : QString();
      showListMenu(key, m_listMore->mapToGlobal(QPoint(0, m_listMore->height())));
    }
  });
  connect(m_listTitle, &QLineEdit::editingFinished, this, [this]() {
    if (m_updating || !canMutate()) {
      return;
    }
    const auto key = m_controller->getProjection().m_listKey;
    const auto *list = m_controller->getDocument().findList(key);
    if (!list) {
      return;
    }
    const auto title = m_listTitle->text();
    if (title.trimmed().isEmpty()) {
      m_listTitle->setText(list->m_title);
      return;
    }
    const QPointer<QLineEdit> input(m_listTitle);
    if (title == list->m_title ||
        command([this, key, title]() { return m_controller->renameList(key, title); })) {
      if (input) {
        // A completed edit returns Undo/Redo ownership to source history.
        input->setText(input->text());
      }
    }
  });
  connect(m_destination, &QComboBox::activated, this,
          [this]() { m_lastDestination = m_destination->currentData().toString(); });
  connect(m_addHasDue, &QCheckBox::toggled, this, [this]() { refreshQuickAdd(); });
  connect(m_addTask, &QLineEdit::textChanged, this, [this]() { refreshQuickAdd(); });
  connect(m_addTask, &QLineEdit::returnPressed, this, &TodoEditor::addTask);
  connect(m_addButton, &QPushButton::clicked, this, &TodoEditor::addTask);
}

void TodoEditor::refreshHeadingFont() {
  auto headingFont = font();
  if (headingFont.pointSizeF() > 0) {
    headingFont.setPointSizeF(qMax(1.0, headingFont.pointSizeF() * 1.35));
  } else {
    headingFont.setPixelSize(qMax(1, qRound(headingFont.pixelSize() * 1.35)));
  }
  m_listTitle->setFont(headingFont);
  m_viewTitle->setFont(headingFont);
  m_viewTitle->setMinimumHeight(m_listTitle->sizeHint().height());
}

void TodoEditor::createDetails() {
  m_detailsScroll = named(new QScrollArea(m_splitter), "todoDetailsScroll", tr("Task details"));
  m_detailsScroll->setWidgetResizable(true);
  m_detailsScroll->setFrameShape(QFrame::NoFrame);
  m_details = named(new QWidget(m_detailsScroll), "todoDetails", tr("Task details"));
  m_detailsScroll->setWidget(m_details);
  auto *layout = new QVBoxLayout(m_details);
  layout->setContentsMargins(12, 12, 12, 12);
  layout->setSpacing(12);
  auto *header = new QHBoxLayout;
  header->addStretch();
  m_back = named(new QToolButton(m_details), "todoBack", tr("Close details and return to tasks"));
  WidgetUtils::setupCloseToolButton(m_back);
  m_back->setToolTip(tr("Close"));
  header->addWidget(m_back, 0, Qt::AlignRight);
  layout->addLayout(header);

  auto *task = new QHBoxLayout;
  task->setSpacing(8);
  m_checked = named(new CompletionCheckBox(m_services.get<ThemeService>(), m_details),
                    "todoChecked", tr("Task completed"));
  m_checked->setToolTip(tr("Task completed"));
  m_title = named(new QLineEdit(m_details), "todoTitle", tr("Task title"));
  m_title->setPlaceholderText(tr("Untitled task"));
  m_title->setStyleSheet(QStringLiteral("QLineEdit { padding: 8px 4px; min-height: 24px; }"));
  m_myDay = named(new QToolButton(m_details), "todoMyDay", tr("Add to My Day"));
  m_myDay->setCheckable(true);
  m_myDay->setAutoRaise(true);
  m_myDay->setIconSize(QSize(20, 20));
  m_myDay->setStyleSheet(QStringLiteral("QToolButton { padding: 8px; }"));
  m_important = named(new QToolButton(m_details), "todoImportant", tr("Task important"));
  m_important->setCheckable(true);
  m_important->setAutoRaise(true);
  m_important->setIconSize(QSize(20, 20));
  m_important->setStyleSheet(QStringLiteral("QToolButton { padding: 8px; }"));
  task->addWidget(m_checked, 0, Qt::AlignVCenter);
  task->addWidget(m_title, 1);
  // Keep the sun physically left of the star without changing the title's RTL layout.
  auto *marks = new QWidget(m_details);
  marks->setLayoutDirection(Qt::LeftToRight);
  auto *markLayout = new QHBoxLayout(marks);
  markLayout->setContentsMargins(0, 0, 0, 0);
  markLayout->setSpacing(8);
  markLayout->addWidget(m_myDay);
  markLayout->addWidget(m_important);
  task->addWidget(marks, 0, Qt::AlignVCenter);
  layout->addLayout(task);

  m_steps = named(new QWidget(m_details), "todoSteps", tr("Task steps"));
  auto *stepIndent = new QHBoxLayout(m_steps);
  stepIndent->setSizeConstraint(QLayout::SetMinimumSize);
  stepIndent->setContentsMargins(0, 0, 0, 0);
  stepIndent->setSpacing(0);
  stepIndent->addSpacing(16);
  auto *stepRows = new QWidget(m_steps);
  stepIndent->addWidget(stepRows, 1);
  m_stepLayout = new QVBoxLayout(stepRows);
  m_stepLayout->setContentsMargins(0, 0, 0, 0);
  m_stepLayout->setSpacing(8);
  m_steps->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
  layout->addWidget(m_steps);
  m_addStep = named(new QLineEdit(m_details), "todoAddStep", tr("Add a step; press Enter"));
  m_addStep->setPlaceholderText(tr("Add a step"));
  m_addStep->setStyleSheet(QStringLiteral("QLineEdit { padding: 10px 12px; min-height: 20px; }"));
  layout->addWidget(m_addStep);
  m_due = named(new QPushButton(m_details), "todoDue", tr("Task due date"));
  m_due->setIconSize(QSize(20, 20));
  // Keep the icon and text together at the requested physical left edge, including in RTL.
  m_due->setLayoutDirection(Qt::LeftToRight);
  m_due->setStyleSheet(
      QStringLiteral("QPushButton { text-align: left; padding: 12px; min-height: 20px; "
                     "min-width: 0; }"));
  layout->addWidget(m_due);
  m_note = named(new SimpleMarkdownEditor(m_services, m_details), "todoNote",
                 tr("Task note in Markdown"));
  named(m_note->getTextEdit(), "todoNoteTextEdit", tr("Task note in Markdown"));
  m_note->getTextEdit()->setPlaceholderText(tr("Add note (markdown supported)"));
  m_note->getTextEdit()->setUndoRedoEnabled(false);
  m_note->setMinimumHeight(144);
  layout->addWidget(m_note, 1);
  auto *footer = new QHBoxLayout;
  footer->setContentsMargins(0, 4, 0, 0);
  footer->setSpacing(8);
  m_createdDate = named(new QLabel(m_details), "todoCreatedDate", tr("Task creation date"));
  m_createdDate->setTextFormat(Qt::PlainText);
  m_createdDate->setAlignment(Qt::AlignCenter);
  m_createdDate->setWordWrap(true);
  m_createdDate->setMinimumWidth(0);
  m_createdDate->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  m_deleteTask = named(new QToolButton(m_details), "todoDeleteTask", tr("Delete task"));
  m_deleteTask->setAutoRaise(true);
  m_deleteTask->setIconSize(QSize(20, 20));
  m_deleteTask->setStyleSheet(QStringLiteral("QToolButton { padding: 8px; }"));
  m_deleteTask->setToolTip(tr("Delete task"));
  footer->addWidget(m_createdDate, 1);
  footer->addWidget(m_deleteTask, 0, Qt::AlignVCenter);
  layout->addLayout(footer);

  auto *theme = m_services.get<ThemeService>();
  const auto refreshIcons = [this, theme]() {
    m_due->setIcon(
        IconUtils::fetchIconWithDisabledState(theme->getIconFile(QStringLiteral("calendar.svg"))));
    m_deleteTask->setIcon(
        IconUtils::fetchIconWithDisabledState(theme->getIconFile(QStringLiteral("delete.svg"))));
    m_back->setIcon(
        IconUtils::fetchIconWithDisabledState(theme->getIconFile(QStringLiteral("close.svg"))));
    // Match the outline/filled stars in the task list, including disabled states.
    QPainterPath path;
    constexpr double pi = 3.14159265358979323846;
    for (int i = 0; i < 10; ++i) {
      const double angle = -pi / 2 + i * pi / 5;
      const double radius = i % 2 ? 4 : 9;
      const QPointF point(10 + std::cos(angle) * radius, 10 + std::sin(angle) * radius);
      if (i == 0) {
        path.moveTo(point);
      } else {
        path.lineTo(point);
      }
    }
    path.closeSubpath();
    QIcon star;
    QIcon sun;
    const qreal scale = devicePixelRatioF();
    for (const auto mode : {QIcon::Normal, QIcon::Disabled}) {
      const QColor color(theme->paletteColor(mode == QIcon::Normal
                                                 ? QStringLiteral("base#normal#fg")
                                                 : QStringLiteral("base#disabled#fg")));
      for (const auto state : {QIcon::Off, QIcon::On}) {
        QPixmap pixmap(qCeil(20 * scale), qCeil(20 * scale));
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(color, 1.5));
        painter.setBrush(state == QIcon::On ? QBrush(color) : Qt::NoBrush);
        painter.drawPath(path);
        painter.end();
        star.addPixmap(pixmap, mode, state);
        QPixmap sunPixmap(qCeil(20 * scale), qCeil(20 * scale));
        sunPixmap.setDevicePixelRatio(scale);
        sunPixmap.fill(Qt::transparent);
        QPainter sunPainter(&sunPixmap);
        sunPainter.setRenderHint(QPainter::Antialiasing);
        paintTodoMyDayIndicator(&sunPainter, QRect(0, 0, 20, 20), state == QIcon::On, color);
        sunPainter.end();
        sun.addPixmap(sunPixmap, mode, state);
      }
    }
    m_important->setIcon(star);
    m_myDay->setIcon(sun);
    const auto more =
        IconUtils::fetchIconWithDisabledState(theme->getIconFile(QStringLiteral("menu.svg")));
    for (const auto &row : m_stepRows) {
      row.m_more->setIcon(more);
    }
  };
  refreshIcons();
  connect(theme, &ThemeService::themeChanged, this, refreshIcons);
  connect(m_back, &QToolButton::clicked, this, &TodoEditor::closeDetails);
  connect(m_deleteTask, &QToolButton::clicked, this, [this]() { deleteTask(m_detailsKey); });
  connect(m_title, &QLineEdit::textEdited, this, [this](const QString &p_text) {
    updateTask([&](TodoTaskFields &p_fields) { p_fields.m_title = p_text; }, m_title);
  });
  connect(m_checked, &QCheckBox::toggled, this, [this](bool p_checked) {
    updateTask([&](TodoTaskFields &p_fields) { p_fields.m_checked = p_checked; });
  });
  connect(m_important, &QToolButton::toggled, this, [this](bool p_checked) {
    updateTask([&](TodoTaskFields &p_fields) { p_fields.m_important = p_checked; });
  });
  connect(m_myDay, &QToolButton::toggled, this, [this](bool p_checked) {
    if (const auto *item = selectedItem()) {
      const auto key = item->m_key;
      command([this, key, p_checked]() { return m_controller->setTaskInMyDay(key, p_checked); });
    }
  });
  connect(m_due, &QPushButton::clicked, this, &TodoEditor::showDueMenu);
  connect(m_note, &SimpleMarkdownEditor::textChanged, this, [this]() {
    if (!m_updating) {
      const auto text = m_note->toPlainText();
      updateTask([&](TodoTaskFields &p_fields) { p_fields.m_note = text; }, m_note->getTextEdit());
    }
  });
  connect(m_addStep, &QLineEdit::returnPressed, this, [this]() {
    const auto *item = selectedItem();
    const auto title = m_addStep->text().trimmed();
    if (!item || title.isEmpty()) {
      return;
    }
    const auto key = item->m_key;
    if (command([this, key, title]() { return m_controller->addStep(key, title); })) {
      m_addStep->clear();
    }
  });
}

void TodoEditor::bindController(TodoViewWindowController *p_controller) {
  if (m_controller == p_controller) {
    return;
  }
  endTyping();
  if (m_controller) {
    disconnect(m_controller, nullptr, this, nullptr);
  }
  if (m_tasks->selectionModel()) {
    disconnect(m_tasks->selectionModel(), nullptr, this, nullptr);
  }
  m_controller = p_controller;
  ++m_generation;
  m_lists->setModel(p_controller ? p_controller->getListModel() : nullptr);
  m_tasks->setModel(p_controller ? p_controller->getTaskModel() : nullptr);
  if (p_controller) {
    connect(p_controller, &TodoViewWindowController::documentChanged, this,
            &TodoEditor::applySortOrder);
    connect(p_controller, &TodoViewWindowController::projectionChanged, this, &TodoEditor::refresh);
    connect(p_controller, &TodoViewWindowController::archiveStateChanged, this,
            &TodoEditor::refresh);
    connect(p_controller, &TodoViewWindowController::selectionChanged, this, [this]() {
      if (m_controller && m_detailsKey != m_controller->getSelectedItemKey()) {
        refresh();
      }
    });
    connect(p_controller, &QObject::destroyed, this, [this]() {
      ++m_generation;
      refresh();
    });
    connect(m_tasks->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &p_current) {
              if (!m_updating && m_controller && p_current.isValid()) {
                const auto key = p_current.data(TodoTaskModel::IdRole).toString();
                if (!key.isEmpty()) {
                  endTyping();
                  m_controller->setSelectedItemKey(key);
                }
              }
            });
  }
  applySortOrder();
  resetForBuffer();
}

void TodoEditor::applySortOrder() {
  if (m_controller) {
    m_controller->setSortOrder(
        m_services.get<ConfigMgr2>()->getEditorConfig().getTodoEditorConfig().getSortOrder());
  }
}

void TodoEditor::setEditable(bool p_editable) {
  if (m_editable != p_editable) {
    endTyping();
    m_editable = p_editable;
    refresh();
  }
}

bool TodoEditor::canMutate() const {
  return m_editable && isEnabled() && m_controller && m_controller->getSnapshotRevision() >= 0 &&
         !m_controller->isArchiving() && m_controller->getDocument().isValid();
}

const TodoItem *TodoEditor::selectedItem() const {
  return m_controller ? m_controller->getDocument().findItem(m_controller->getSelectedItemKey())
                      : nullptr;
}

bool TodoEditor::preservesField(QWidget *p_field) const {
  return m_committing && m_typingField == p_field;
}

void TodoEditor::refresh() {
  if (m_updating) {
    return;
  }
  QScopedValueRollback<bool> updating(m_updating, true);
  const bool writable = canMutate();
  m_newList->setEnabled(writable);
  m_newGroup->setEnabled(writable);
  const bool archiveEnabled = writable && m_controller->canArchiveCompletedTasks();
  m_archiveCompleted->setEnabled(archiveEnabled);
  m_archiveCompleted->setToolTip(
      archiveEnabled
          ? tr("Archive Completed Tasks")
          : tr("Archive Completed Tasks (requires completed tasks in an editable file)"));
  m_lists->setDragEnabled(writable);
  m_lists->setAcceptDrops(writable);
  m_listTitle->setReadOnly(!writable);
  if (!m_controller) {
    m_listTitle->hide();
    m_listIcon->hide();
    m_description->hide();
    m_listMore->hide();
    m_viewTitle->setText(tr("All Tasks"));
    refreshDetails();
    refreshQuickAdd();
    return;
  }
  const auto &projection = m_controller->getProjection();
  const int row = m_controller->getListModel()->rowForView(projection.m_view, projection.m_listKey);
  auto *listModel = m_controller->getListModel();
  for (int i = 0; i < listModel->rowCount(); ++i) {
    const auto index = listModel->index(i, 0);
    m_lists->setRowHidden(i, listModel->listAt(i) &&
                                 !index.data(TodoListModel::ExpandedRole).toBool());
  }
  int groupRow = listModel->rowForGroup(projection.m_groupKey);
  if (m_groupFocused && groupRow < 0) {
    const auto &document = m_controller->getDocument();
    const auto *group = document.findGroup(projection.m_groupKey);
    if (group && group->m_isDefault && group->m_listCount > 0) {
      groupRow = listModel->rowForKey(document.m_lists.at(group->m_firstList).m_key);
    }
  }
  const int selectedRow =
      (m_groupFocused || (row >= 0 && m_lists->isRowHidden(row))) && groupRow >= 0 ? groupRow : row;
  m_lists->setCurrentIndex(listModel->index(selectedRow, 0));
  if (m_search->text() != projection.m_search) {
    m_search->setText(projection.m_search);
  }
  const bool filtered = !projection.m_search.isEmpty() || !projection.m_tag.isEmpty();
  const auto *list = projection.m_view == TodoViewKind::List && !filtered
                         ? m_controller->getDocument().findList(projection.m_listKey)
                         : nullptr;
  m_tasks->setShowLocation(!list);
  m_listTitle->setVisible(list != nullptr);
  m_listIcon->setVisible(list != nullptr);
  m_listIcon->setEnabled(writable);
  refreshListIcon();
  m_description->setVisible(list && !list->m_description.isEmpty());
  m_listMore->show();
  m_listMore->setEnabled(m_controller->getDocument().isValid() &&
                         m_controller->getSnapshotRevision() >= 0);
  m_viewTitle->setVisible(!list);
  if (list) {
    if (!preservesField(m_listTitle) && m_listTitle->text() != list->m_title) {
      m_listTitle->setText(list->m_title);
    }
    if (m_description->text() != list->m_description) {
      m_description->setText(list->m_description);
    }
  } else {
    m_viewTitle->setText(filtered ? tr("Search results")
                                  : m_controller->getListModel()->index(row, 0).data().toString());
  }
  const auto selected = m_controller->getTaskModel()->index(
      m_controller->getTaskModel()->rowForKey(m_controller->getSelectedItemKey()), 0);
  if (m_tasks->currentIndex() != selected) {
    m_tasks->setCurrentIndex(selected);
  }
  m_tasks->setDragEnabled(writable && m_controller->getTaskModel()->isReorderingEnabled());
  m_tasks->setAcceptDrops(writable && m_controller->getTaskModel()->isReorderingEnabled());
  const QString destination = m_destination->currentData().toString();
  m_destination->clear();
  for (const auto &entry : m_controller->getDocument().m_lists) {
    m_destination->addItem(qualifiedListName(m_controller->getDocument(), entry), entry.m_key);
  }
  int destinationIndex = m_destination->findData(m_lastDestination);
  if (destinationIndex < 0) {
    destinationIndex = m_destination->findData(destination);
  }
  m_destination->setCurrentIndex(destinationIndex < 0 ? 0 : destinationIndex);
  refreshDetails();
  refreshQuickAdd();
}

void TodoEditor::refreshDetails() {
  const auto *item = selectedItem();
  const QString key = item ? item->m_key : QString();
  const bool changed = key != m_detailsKey && !(m_committing && m_typingField);
  if (changed) {
    endTyping();
    m_addStep->clear();
  }
  m_detailsKey = key;
  if (!item) {
    m_detailsOpen = false;
  }
  const bool writable = canMutate() && item;
  const TodoTaskFields emptyFields;
  const auto &fields = item ? item->m_fields : emptyFields;
  m_title->setReadOnly(!writable);
  m_note->setReadOnly(!writable);
  m_checked->setEnabled(writable);
  m_important->setEnabled(writable);
  m_myDay->setEnabled(writable);
  m_due->setEnabled(writable);
  m_deleteTask->setEnabled(writable);
  m_createdDate->setText(item && item->m_createdAt.isValid()
                             ? tr("Created on %1")
                                   .arg(QLocale().toString(item->m_createdAt.toLocalTime().date(),
                                                           QLocale::LongFormat))
                             : tr("Creation date unknown"));
  m_createdDate->setAccessibleDescription(m_createdDate->text());
  m_addStep->setEnabled(writable);
  if (!preservesField(m_title) && m_title->text() != fields.m_title) {
    m_title->setText(fields.m_title);
  }
  m_checked->setChecked(fields.m_checked);
  m_important->setChecked(fields.m_important);
  m_important->setToolTip(fields.m_important ? tr("Remove importance") : tr("Mark as important"));
  m_myDay->setChecked(m_controller && fields.m_myDay.isValid() &&
                      fields.m_myDay == m_controller->getProjection().m_today);
  const auto dayAction = m_myDay->isChecked() ? tr("Remove from My Day") : tr("Add to My Day");
  m_myDay->setAccessibleName(dayAction);
  const auto dayDescription =
      fields.m_myDay.isValid()
          ? tr("My Day: %1").arg(QLocale().toString(fields.m_myDay, QLocale::ShortFormat))
          : tr("Include task in My Day");
  m_myDay->setToolTip(dayAction + QLatin1Char('\n') + dayDescription);
  m_myDay->setAccessibleDescription(dayDescription);
  m_due->setText(fields.m_due.isValid() ? QLocale().toString(fields.m_due, QLocale::ShortFormat)
                                        : tr("Add Due Date"));
  m_due->setAccessibleDescription(m_due->text());
  if (!preservesField(m_note->getTextEdit()) && m_note->toPlainText() != fields.m_note) {
    m_note->setPlainText(fields.m_note);
  }
  refreshSteps(item, changed);
  updateLayout();
}

void TodoEditor::refreshSteps(const TodoItem *p_item, bool p_reset) {
  const int count = p_item ? int(p_item->m_steps.size()) : 0;
  const bool writable = canMutate() && p_item;
  // Own typing echoes retain the focused field, cursor and IME preedit. Structural
  // changes retire all row callbacks before indexes are assigned to new controls.
  if (p_reset || count != m_stepRows.size()) {
    endTyping();
    for (const auto &row : m_stepRows) {
      m_stepLayout->removeWidget(row.m_widget);
      row.m_widget->hide();
      row.m_widget->setEnabled(false);
      for (auto *child : row.m_widget->findChildren<QWidget *>()) {
        disconnect(child, nullptr, this, nullptr);
        child->setObjectName({});
      }
      row.m_widget->deleteLater();
    }
    m_stepRows.clear();
    if (count) {
      const auto moreIcon = IconUtils::fetchIconWithDisabledState(
          m_services.get<ThemeService>()->getIconFile(QStringLiteral("menu.svg")));
      m_stepRows.reserve(count);
      for (int i = 0; i < count; ++i) {
        auto *widget = new QWidget(m_steps);
        auto *layout = new QHBoxLayout(widget);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        auto *checked = new CompletionCheckBox(m_services.get<ThemeService>(), widget);
        checked->setObjectName(QStringLiteral("todoStepChecked_%1").arg(i));
        auto *title = new QLineEdit(widget);
        title->setObjectName(QStringLiteral("todoStepTitle_%1").arg(i));
        title->setAccessibleName(tr("Step title"));
        title->setPlaceholderText(tr("Untitled step"));
        title->setStyleSheet(QStringLiteral("QLineEdit { padding: 8px 4px; min-height: 20px; }"));
        auto *more = new QToolButton(widget);
        more->setObjectName(QStringLiteral("todoStepMore_%1").arg(i));
        more->setIcon(moreIcon);
        more->setIconSize(QSize(20, 20));
        more->setAutoRaise(true);
        more->setStyleSheet(QStringLiteral("QToolButton { padding: 8px; }"));
        more->setToolTip(tr("Step options"));
        layout->addWidget(checked, 0, Qt::AlignVCenter);
        layout->addWidget(title, 1);
        layout->addWidget(more, 0, Qt::AlignVCenter);
        m_stepLayout->addWidget(widget);
        m_stepRows.append({widget, checked, title, more});
        connect(title, &QLineEdit::textEdited, this, [this, i]() { updateStep(i, true); });
        connect(checked, &QCheckBox::toggled, this, [this, i]() { updateStep(i, false); });
        connect(more, &QToolButton::clicked, this, [this, i, more]() {
          showStepMenu(i, more->mapToGlobal(QPoint(0, more->height())));
        });
      }
    }
  }
  for (int i = 0; i < count; ++i) {
    const auto &step = p_item->m_steps[i];
    const auto &row = m_stepRows[i];
    const auto title = step.m_title.trimmed().isEmpty() ? tr("Untitled step") : step.m_title;
    row.m_checked->setAccessibleName(tr("Complete step: %1").arg(title));
    row.m_checked->setToolTip(row.m_checked->accessibleName());
    row.m_checked->setEnabled(writable);
    row.m_checked->setChecked(step.m_checked);
    row.m_title->setReadOnly(!writable);
    if (row.m_title->font().strikeOut() != step.m_checked) {
      QFont font;
      font.setStrikeOut(step.m_checked);
      row.m_title->setFont(font);
    }
    if (!preservesField(row.m_title) && row.m_title->text() != step.m_title) {
      row.m_title->setText(step.m_title);
    }
    row.m_more->setAccessibleName(tr("Options for step: %1").arg(title));
    row.m_more->setEnabled(writable);
  }
  m_steps->setVisible(count > 0);
}

void TodoEditor::refreshQuickAdd() {
  const auto projection = m_controller ? m_controller->getProjection() : TodoProjection();
  const bool filtered = !projection.m_search.isEmpty() || !projection.m_tag.isEmpty();
  const bool destination = filtered || projection.m_view != TodoViewKind::List;
  const bool planned = !filtered && projection.m_view == TodoViewKind::Planned;
  m_destination->setVisible(destination && m_destination->count() > 0);
  m_destination->setEnabled(canMutate());
  m_addTask->setEnabled(canMutate());
  m_addHasDue->setVisible(planned);
  m_addDue->setVisible(planned);
  m_addHasDue->setEnabled(canMutate() && planned);
  m_addDue->setEnabled(canMutate() && planned && m_addHasDue->isChecked());
  m_addButton->setEnabled(canMutate() && !m_addTask->text().trimmed().isEmpty() &&
                          (!planned || m_addHasDue->isChecked()));
  m_addTask->setToolTip(planned && !m_addHasDue->isChecked()
                            ? tr("Choose a due date before adding to Planned")
                            : QString());
}

void TodoEditor::updateLayout(bool p_resized) {
  const bool narrow = width() < 840;
  const bool layoutChanged = narrow != m_narrow;
  if (layoutChanged) {
    const QPointer<QWidget> focused = QApplication::focusWidget();
    const bool sidebarFocused = focused && m_sidebar->isAncestorOf(focused);
    m_narrow = narrow;
    m_listsButton->setChecked(false);
    if (narrow) {
      m_sidebar->setParent(this);
      m_sidebar->hide();
      m_pages->addWidget(m_detailsScroll);
      m_navigation->show();
      if (sidebarFocused) {
        m_listsButton->setFocus(Qt::OtherFocusReason);
      }
    } else {
      m_splitter->insertWidget(0, m_sidebar);
      m_pages->removeWidget(m_detailsScroll);
      m_splitter->addWidget(m_detailsScroll);
      m_sidebar->show();
      m_navigation->hide();
      m_pages->setCurrentWidget(m_taskPane);
      m_splitter->setStretchFactor(0, 0);
      m_splitter->setStretchFactor(1, 1);
      m_splitter->setStretchFactor(2, 0);
      if (sidebarFocused && focused) {
        focused->setFocus(Qt::OtherFocusReason);
      }
    }
  }
  if (narrow) {
    m_pages->setCurrentWidget(m_detailsOpen ? static_cast<QWidget *>(m_detailsScroll) : m_taskPane);
    updateSidebarGeometry();
  } else {
    const bool detailsChanged = m_detailsScroll->isHidden() == m_detailsOpen;
    m_detailsScroll->setVisible(m_detailsOpen);
    if (p_resized || layoutChanged || detailsChanged) {
      // Restore after visibility/reparenting changes; hidden Qt allocations may be zero.
      const int available =
          m_splitter->contentsRect().width() - m_splitter->handleWidth() * (m_detailsOpen ? 2 : 1);
      const int tasksWidth =
          static_cast<int>(qMax<qint64>(1, qint64(available) - m_paneLayout.m_listsWidth -
                                               (m_detailsOpen ? m_paneLayout.m_detailsWidth : 0)));
      m_splitter->setSizes({m_paneLayout.m_listsWidth, tasksWidth, m_paneLayout.m_detailsWidth});
    }
  }
}

void TodoEditor::updateSidebarGeometry() {
  if (!m_narrow || !m_listsButton->isChecked()) {
    return;
  }
  const auto bounds = m_splitter->geometry();
  const int panelWidth = qMin(qMax(260, m_sidebar->minimumSizeHint().width()), bounds.width());
  const int left = isRightToLeft() ? bounds.right() - panelWidth + 1 : bounds.left();
  m_sidebar->setGeometry(left, bounds.top(), panelWidth, bounds.height());
  m_sidebar->raise();
}

void TodoEditor::setSidebarOpen(bool p_open) {
  if (!m_narrow) {
    return;
  }
  m_listsButton->setChecked(p_open);
  if (p_open) {
    endTyping();
    updateSidebarGeometry();
    m_sidebar->show();
    m_lists->setFocus(Qt::OtherFocusReason);
    m_lists->scrollTo(m_lists->currentIndex());
  } else {
    const auto *focused = QApplication::focusWidget();
    const bool sidebarFocused = focused && m_sidebar->isAncestorOf(focused);
    m_sidebar->hide();
    m_searchComposing = false;
    if (sidebarFocused && isVisible()) {
      m_listsButton->setFocus(Qt::OtherFocusReason);
    }
  }
}

void TodoEditor::resizeEvent(QResizeEvent *p_event) {
  QWidget::resizeEvent(p_event);
  updateLayout(true);
}

void TodoEditor::selectView(int p_row) {
  if (m_updating || !m_controller || p_row < 0) {
    return;
  }
  const auto index = m_controller->getListModel()->index(p_row, 0);
  if (!index.isValid() || index.data(TodoListModel::GroupRole).toBool()) {
    return;
  }
  const auto view = index.data(TodoListModel::ViewKindRole).value<TodoViewKind>();
  const auto key = index.data(TodoListModel::IdRole).toString();
  const auto groupKey = index.data(TodoListModel::GroupIdRole).toString();
  endTyping();
  m_groupFocused = false;
  if (!groupKey.isEmpty()) {
    m_controller->setGroupExpanded(groupKey, true);
  }
  m_controller->setView(view, key);
  refresh();
  if (m_narrow) {
    closeDetails();
  }
}

void TodoEditor::openTask(const QModelIndex &p_index) {
  if (!m_controller || !p_index.isValid() || p_index.data(TodoTaskModel::SectionRole).toBool()) {
    return;
  }
  const auto key = p_index.data(TodoTaskModel::IdRole).toString();
  if (key.isEmpty() || !m_controller->getDocument().findItem(key)) {
    return;
  }
  const auto groupKey = p_index.data(TodoTaskModel::SourceGroupIdRole).toString();
  endTyping();
  m_groupFocused = false;
  m_controller->setGroupExpanded(groupKey, true);
  m_controller->setSelectedGroupKey(groupKey);
  m_controller->setSelectedItemKey(key);
  m_detailsOpen = true;
  updateLayout();
  if (m_narrow) {
    m_title->setFocus(Qt::OtherFocusReason);
  }
}

void TodoEditor::closeDetails() {
  endTyping();
  m_detailsOpen = false;
  updateLayout();
  const auto *item = selectedItem();
  if (item && item->m_fields.m_checked && !m_controller->getProjection().m_completedExpanded &&
      m_controller->getTaskModel()->rowForKey(item->m_key) < 0) {
    // Reveal the originating completed row before restoring keyboard focus.
    m_controller->setCompletedExpanded(true);
  }
  focusTasks(true);
}

void TodoEditor::focusSearch() {
  if (m_narrow && m_detailsOpen) {
    closeDetails();
  }
  setSidebarOpen(true);
  m_search->setFocus(Qt::ShortcutFocusReason);
  m_search->selectAll();
}

QString TodoEditor::selectedGroupKey() const {
  return m_controller ? m_controller->getProjection().m_groupKey : QString();
}

void TodoEditor::focusGroup(const QString &p_key) {
  if (!m_controller || !m_controller->getDocument().findGroup(p_key)) {
    return;
  }
  endTyping();
  m_groupFocused = true;
  m_controller->setSelectedGroupKey(p_key);
  refresh();
  setSidebarOpen(true);
  m_lists->setFocus(Qt::OtherFocusReason);
  m_lists->scrollTo(m_lists->currentIndex());
}

void TodoEditor::toggleGroup(const QString &p_key) {
  if (!m_controller || !m_controller->getDocument().findGroup(p_key)) {
    return;
  }
  endTyping();
  m_groupFocused = true;
  m_controller->setSelectedGroupKey(p_key);
  m_controller->setGroupExpanded(p_key,
                                 m_controller->getProjection().m_collapsedGroups.contains(p_key));
}

void TodoEditor::focusCurrent() {
  if (m_narrow && m_detailsOpen) {
    m_title->setFocus(Qt::OtherFocusReason);
  } else {
    focusTasks();
  }
}

void TodoEditor::focusTasks(bool p_restoreTaskSelection) {
  setSidebarOpen(false);
  m_groupFocused = false;
  if (m_controller) {
    auto *model = m_controller->getListModel();
    const auto &projection = m_controller->getProjection();
    const int row = model->rowForView(projection.m_view, projection.m_listKey);
    if (row >= 0 && !m_lists->isRowHidden(row)) {
      m_lists->setCurrentIndex(model->index(row, 0));
    }
    if (p_restoreTaskSelection) {
      auto *tasks = m_controller->getTaskModel();
      const auto selected = tasks->index(tasks->rowForKey(m_controller->getSelectedItemKey()));
      if (selected.isValid() && m_tasks->currentIndex() != selected) {
        m_tasks->setCurrentIndex(selected);
      }
    }
  }
  if (m_narrow && m_detailsOpen) {
    m_detailsOpen = false;
    updateLayout();
  }
  m_tasks->setFocus(Qt::OtherFocusReason);
  if (m_tasks->currentIndex().isValid()) {
    m_tasks->scrollTo(m_tasks->currentIndex());
  }
}

void TodoEditor::endTyping() {
  m_typingField.clear();
  ++m_typingSession;
  if (m_controller) {
    m_controller->endCoalescing();
  }
}

template <typename Command>
bool TodoEditor::command(const Command &p_command, QWidget *p_typingField) {
  if (m_updating || !canMutate()) {
    return false;
  }
  if (p_typingField) {
    if (m_typingField != p_typingField) {
      endTyping();
      m_typingField = p_typingField;
    }
    // The session token survives source:-to-id: materialization on the first keystroke.
    m_controller->setCoalescingKey(
        QStringLiteral("native:%1:%2").arg(m_typingSession).arg(p_typingField->objectName()));
  } else {
    endTyping();
  }
  const QPointer<TodoEditor> self(this);
  m_committing = true;
  const bool result = p_command();
  if (!self) {
    return result;
  }
  m_committing = false;
  if (!result) {
    endTyping();
    refresh();
  }
  return result;
}

template <typename Update>
void TodoEditor::updateTask(const Update &p_update, QWidget *p_typingField) {
  if (m_updating || !canMutate()) {
    return;
  }
  const auto *item = selectedItem();
  if (!item) {
    return;
  }
  const auto key = item->m_key;
  auto fields = item->m_fields;
  p_update(fields);
  command([this, key, fields]() { return m_controller->updateTask(key, fields); }, p_typingField);
}

void TodoEditor::addTask() {
  if (!m_addButton->isEnabled() || !m_controller) {
    return;
  }
  const auto &projection = m_controller->getProjection();
  const bool filtered = !projection.m_search.isEmpty() || !projection.m_tag.isEmpty();
  QString listKey = !filtered && projection.m_view == TodoViewKind::List
                        ? projection.m_listKey
                        : m_destination->currentData().toString();
  TodoTaskFields fields;
  fields.m_title = m_addTask->text().trimmed();
  if (!filtered && projection.m_view == TodoViewKind::MyDay) {
    fields.m_myDay = QDate::currentDate();
  }
  if (!filtered && projection.m_view == TodoViewKind::Important) {
    fields.m_important = true;
  }
  if (!filtered && projection.m_view == TodoViewKind::Planned && m_addHasDue->isChecked()) {
    fields.m_due = m_addDue->date();
  }
  const QPointer<TodoEditor> self(this);
  const QPointer<TodoViewWindowController> controller(m_controller);
  const auto generation = m_generation;
  if (command([this, self, controller, generation, listKey, fields]() {
        if (fields.m_myDay.isValid()) {
          // Keep the captured draft/destination, but show the new day before selecting its task.
          controller->setToday(fields.m_myDay);
          if (!self || !controller || m_controller != controller || m_generation != generation) {
            return false;
          }
        }
        return controller->addTask(listKey, fields);
      }) &&
      self) {
    if (const auto *added = selectedItem()) {
      m_lastDestination = added->m_listKey;
      const auto *list = m_controller->getDocument().findList(m_lastDestination);
      const auto *group = list ? m_controller->getDocument().groupForList(*list) : nullptr;
      if (group) {
        const auto groupKey = group->m_key;
        m_groupFocused = false;
        m_controller->setSelectedGroupKey(groupKey);
        m_controller->setGroupExpanded(groupKey, true);
      }
      m_destination->setCurrentIndex(m_destination->findData(m_lastDestination));
    }
    m_addTask->clear();
    m_addTask->setFocus(Qt::OtherFocusReason);
  }
}

void TodoEditor::updateStep(int p_index, bool p_typing) {
  if (m_updating || !canMutate()) {
    return;
  }
  const auto *item = selectedItem();
  if (!item || p_index < 0 || p_index >= item->m_steps.size() || p_index >= m_stepRows.size()) {
    return;
  }
  const auto key = item->m_key;
  const auto &row = m_stepRows[p_index];
  const auto title = row.m_title->text();
  const bool checked = row.m_checked->isChecked();
  command([this, key, p_index, title,
           checked]() { return m_controller->updateStep(key, p_index, title, checked); },
          p_typing ? row.m_title : nullptr);
}

void TodoEditor::showStepMenu(int p_index, const QPoint &p_globalPosition) {
  const auto *item = selectedItem();
  if (!item || p_index < 0 || p_index >= item->m_steps.size() || !canMutate()) {
    return;
  }
  endTyping();
  const auto token = gesture();
  const auto key = item->m_key;
  const auto &step = item->m_steps[p_index];
  const bool checked = step.m_checked;
  const auto title = step.m_title;
  auto *menu = named(new QMenu(this), "todoStepMenu", tr("Step options"));
  menu->setAttribute(Qt::WA_DeleteOnClose);
  const auto add = [&](const QString &p_label, const char *p_name,
                       const std::function<bool()> &p_command) {
    auto *action = menu->addAction(p_label);
    action->setObjectName(QString::fromLatin1(p_name));
    connect(action, &QAction::triggered, this, [this, token, p_command]() {
      if (accepts(token)) {
        command(p_command);
      }
    });
  };
  add(checked ? tr("Mark as Not Completed") : tr("Mark as Completed"),
      "todoToggleStepCompletedAction", [this, key, p_index, title, checked]() {
        return m_controller->updateStep(key, p_index, title, !checked);
      });
  if (!checked) {
    add(tr("Promote to Task"), "todoPromoteStepAction",
        [this, key, p_index]() { return m_controller->promoteStep(key, p_index); });
  }
  add(tr("Delete"), "todoDeleteStepAction",
      [this, key, p_index]() { return m_controller->removeStep(key, p_index); });
  menu->popup(p_globalPosition);
}

void TodoEditor::showDueMenu() {
  const auto *item = selectedItem();
  if (!item || !canMutate()) {
    return;
  }
  endTyping();
  const auto token = gesture();
  const auto today = m_controller->getProjection().m_today;
  auto *menu = named(new QMenu(this), "todoDueMenu", tr("Task due date"));
  menu->setAttribute(Qt::WA_DeleteOnClose);
  const auto addDate = [&](const QString &p_label, const char *p_name, const QDate &p_date) {
    auto *action = menu->addAction(p_label);
    action->setObjectName(QString::fromLatin1(p_name));
    connect(action, &QAction::triggered, this, [this, token, p_date]() {
      if (accepts(token)) {
        updateTask([&](TodoTaskFields &p_fields) { p_fields.m_due = p_date; });
      }
    });
  };
  addDate(tr("Today"), "todoDueTodayAction", today);
  addDate(tr("Tomorrow"), "todoDueTomorrowAction", today.addDays(1));
  addDate(tr("Next Week"), "todoDueNextWeekAction", today.addDays(7));
  auto *pick = menu->addAction(tr("Pick A Date"));
  pick->setObjectName(QStringLiteral("todoPickDueDateAction"));
  connect(pick, &QAction::triggered, this, [this, token]() {
    if (accepts(token)) {
      pickDueDate();
    }
  });
  if (item->m_fields.m_due.isValid()) {
    menu->addSeparator();
    addDate(tr("Remove Due Date"), "todoClearDueAction", {});
  }
  menu->popup(m_due->mapToGlobal(QPoint(0, m_due->height())));
}

void TodoEditor::pickDueDate() {
  const auto *item = selectedItem();
  if (!item || !canMutate()) {
    return;
  }
  const auto token = gesture();
  auto *dialog = named(new QDialog(this), "todoDueDateDialog", tr("Pick A Date"));
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("Pick A Date"));
  auto *layout = new QVBoxLayout(dialog);
  auto *calendar = named(new QCalendarWidget(dialog), "todoDueCalendar", tr("Task due date"));
  calendar->setSelectedDate(item->m_fields.m_due.isValid() ? item->m_fields.m_due
                                                           : m_controller->getProjection().m_today);
  layout->addWidget(calendar);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
  named(buttons->button(QDialogButtonBox::Ok), "todoDueDateAccept", tr("Set due date"));
  named(buttons->button(QDialogButtonBox::Cancel), "todoDueDateCancel", tr("Cancel due date"));
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  connect(dialog, &QDialog::accepted, this, [this, token, calendar]() {
    if (accepts(token)) {
      const auto date = calendar->selectedDate();
      updateTask([&](TodoTaskFields &p_fields) { p_fields.m_due = date; });
    }
  });
  dialog->open();
}

TodoEditor::Gesture TodoEditor::gesture() const {
  return {m_controller, m_generation,
          m_controller ? m_controller->getDocument().m_source : QString(),
          m_controller ? m_controller->getSelectedItemKey() : QString(), selectedGroupKey()};
}

bool TodoEditor::accepts(const Gesture &p_gesture) const {
  return canMutate() && acceptsNavigation(p_gesture);
}

bool TodoEditor::acceptsNavigation(const Gesture &p_gesture) const {
  return isEnabled() && m_controller && p_gesture.m_controller == m_controller &&
         p_gesture.m_generation == m_generation &&
         p_gesture.m_source == m_controller->getDocument().m_source &&
         p_gesture.m_selection == m_controller->getSelectedItemKey() &&
         p_gesture.m_groupSelection == selectedGroupKey();
}

void TodoEditor::addList(const QString &p_groupKey) {
  if (!canMutate()) {
    return;
  }
  endTyping();
  const auto token = gesture();
  const QPointer<TodoEditor> self(this);
  QPointer<QDialog> dialog = named(new QDialog(this), "todoNewListDialog", tr("New list"));
  dialog->setWindowTitle(tr("New List"));
  auto *layout = new QVBoxLayout(dialog);
  spacing(layout);
  auto *titleInput = named(new QLineEdit(dialog), "todoNewListName", tr("List name"));
  titleInput->setPlaceholderText(tr("List name"));
  layout->addWidget(titleInput);
  auto *groups = named(new QComboBox(dialog), "todoNewListGroup", tr("Group for the new list"));
  addGroupChoices(groups, m_controller->getDocument());
  const auto *group = m_controller->getDocument().findGroup(p_groupKey);
  groups->setCurrentIndex(group && !group->m_isDefault ? groups->findData(p_groupKey) : 0);
  layout->addWidget(groups);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
  named(buttons->button(QDialogButtonBox::Ok), "todoNewListAccept", tr("Create list"));
  named(buttons->button(QDialogButtonBox::Cancel), "todoNewListCancel", tr("Cancel new list"));
  buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
  connect(titleInput, &QLineEdit::textChanged, buttons, [buttons](const QString &p_text) {
    buttons->button(QDialogButtonBox::Ok)->setEnabled(!p_text.trimmed().isEmpty());
  });
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  const int result = dialog->exec();
  if (!dialog) {
    return;
  }
  const auto title = titleInput->text().trimmed();
  const auto groupKey = groups->currentData().toString();
  delete dialog;
  if (!self || result != QDialog::Accepted || !accepts(token) || title.isEmpty()) {
    return;
  }
  const QPointer<TodoViewWindowController> controller(m_controller);
  QString createdKey;
  if (command([controller, title, groupKey, &createdKey]() {
        return controller->addList(title, groupKey, &createdKey);
      }) &&
      self && m_controller == controller) {
    m_groupFocused = false;
    const auto *list = controller->getDocument().findList(createdKey);
    if (list) {
      const auto *owner = controller->getDocument().groupForList(*list);
      if (owner) {
        controller->setGroupExpanded(owner->m_key, true);
      }
      controller->setView(TodoViewKind::List, createdKey);
      if (m_narrow) {
        closeDetails();
      }
    }
  }
}

void TodoEditor::addGroup() {
  if (!canMutate()) {
    return;
  }
  endTyping();
  const auto token = gesture();
  const QPointer<TodoEditor> self(this);
  QPointer<QInputDialog> dialog =
      named(new QInputDialog(this), "todoNewGroupDialog", tr("New group"));
  dialog->setInputMode(QInputDialog::TextInput);
  dialog->setWindowTitle(tr("New Group"));
  dialog->setLabelText(tr("Group name"));
  if (auto *input = dialog->findChild<QLineEdit *>()) {
    named(input, "todoNewGroupName", tr("Group name"));
  }
  const int result = dialog->exec();
  if (!dialog) {
    return;
  }
  const auto title = dialog->textValue().trimmed();
  delete dialog;
  if (!self || result != QDialog::Accepted || !accepts(token) || title.isEmpty()) {
    return;
  }
  const QPointer<TodoViewWindowController> controller(m_controller);
  QString createdKey;
  if (command([controller, title, &createdKey]() {
        return controller->addGroup(title, &createdKey);
      }) &&
      self && m_controller == controller) {
    focusGroup(createdKey);
  }
}

void TodoEditor::archiveCompletedTasks() {
  if (!canMutate() || !m_controller->canArchiveCompletedTasks())
    return;
  endTyping();
  const auto token = gesture();
  const QPointer<TodoEditor> self(this);
  const auto answer = QMessageBox::question(
      this, tr("Archive Completed Tasks"),
      tr("Archive all completed tasks in this file?\n\n"
         "They will be moved, with their group and list hierarchy, to a new .archived.todo "
         "file in the same folder. All groups and lists will remain in this file, even if empty."),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (self && answer == QMessageBox::Yes && accepts(token)) {
    const QPointer<TodoViewWindowController> controller(m_controller);
    command([controller]() { return controller && controller->archiveCompletedTasks(); });
  }
}

void TodoEditor::renameGroup(const QString &p_key) {
  if (!canMutate()) {
    return;
  }
  const auto *group = m_controller->getDocument().findGroup(p_key);
  if (!group) {
    return;
  }
  endTyping();
  const auto title = group->m_title;
  const auto token = gesture();
  const QPointer<TodoEditor> self(this);
  QPointer<QInputDialog> dialog =
      named(new QInputDialog(this), "todoRenameGroupDialog", tr("Rename group"));
  dialog->setInputMode(QInputDialog::TextInput);
  dialog->setWindowTitle(tr("Rename group"));
  dialog->setLabelText(tr("Group name"));
  dialog->setTextValue(title);
  if (auto *input = dialog->findChild<QLineEdit *>()) {
    named(input, "todoRenameGroupName", tr("Group name"));
  }
  const int result = dialog->exec();
  if (!dialog) {
    return;
  }
  const auto renamed = dialog->textValue();
  delete dialog;
  if (self && result == QDialog::Accepted && accepts(token)) {
    command([this, p_key, renamed]() { return m_controller->renameGroup(p_key, renamed); });
  }
}

void TodoEditor::moveGroup(const QString &p_key, int p_direction) {
  if (!canMutate()) {
    return;
  }
  const auto &groups = m_controller->getDocument().m_groups;
  int position = -1;
  for (int i = 0; i < groups.size(); ++i) {
    if (!groups[i].m_isDefault && groups[i].m_key == p_key) {
      position = i;
      break;
    }
  }
  if (position < 0) {
    return;
  }
  // Only user groups participate in the visible order; Default is an internal owner.
  int target = position + p_direction;
  while (target >= 0 && target < groups.size() && groups[target].m_isDefault) {
    target += p_direction;
  }
  if (target < 0 || target >= groups.size()) {
    return;
  }
  int before = target;
  if (p_direction > 0) {
    ++before;
    while (before < groups.size() && groups[before].m_isDefault) {
      ++before;
    }
  }
  const QString beforeKey = before < groups.size() ? groups[before].m_key : QString();
  command([this, p_key, beforeKey]() { return m_controller->moveGroup(p_key, beforeKey); });
}

void TodoEditor::showGroupMenu(const QString &p_key, const QPoint &p_globalPosition) {
  const auto *group = m_controller ? m_controller->getDocument().findGroup(p_key) : nullptr;
  if (!group || group->m_isDefault) {
    return;
  }
  endTyping();
  m_groupFocused = true;
  m_controller->setSelectedGroupKey(p_key);
  const auto token = gesture();
  auto *menu = named(new QMenu(this), "todoGroupMenu", tr("Group actions"));
  menu->setAttribute(Qt::WA_DeleteOnClose);
  auto add = [&](const QString &p_label, const char *p_name,
                 const std::function<void()> &p_action) {
    auto *action = menu->addAction(p_label);
    action->setObjectName(QString::fromLatin1(p_name));
    action->setEnabled(canMutate());
    connect(action, &QAction::triggered, this, [this, token, p_action]() {
      if (accepts(token)) {
        p_action();
      }
    });
  };
  add(tr("Rename"), "todoRenameGroupAction", [this, p_key]() { renameGroup(p_key); });
  add(tr("New List"), "todoGroupNewListAction", [this, p_key]() { addList(p_key); });
  add(tr("Ungroup Lists"), "todoUngroupListsAction",
      [this, p_key]() { command([this, p_key]() { return m_controller->ungroupLists(p_key); }); });
  menu->popup(p_globalPosition);
}

void TodoEditor::renameList(const QString &p_key) {
  if (!canMutate()) {
    return;
  }
  const auto *list = m_controller->getDocument().findList(p_key);
  if (!list) {
    return;
  }
  endTyping();
  m_controller->setView(TodoViewKind::List, p_key);
  m_controller->setSearch({});
  m_controller->setTagFilter({});
  if (m_narrow) {
    closeDetails();
  }
  m_listTitle->setFocus(Qt::OtherFocusReason);
  m_listTitle->selectAll();
}

void TodoEditor::editListDescription(const QString &p_key) {
  if (!canMutate()) {
    return;
  }
  const QString key(p_key);
  const auto *list = m_controller->getDocument().findList(key);
  if (!list) {
    return;
  }
  endTyping();
  const auto token = gesture();
  const QPointer<TodoEditor> self(this);
  // Keep local editing shortcuts outside the TODO window's source-history routing.
  QDialog dialog;
  named(&dialog, "todoListDescriptionDialog", tr("Edit Description"));
  dialog.setWindowTitle(tr("Edit Description"));
  auto *layout = new QVBoxLayout(&dialog);
  spacing(layout);
  auto *input =
      named(new PlainTextEdit(&dialog), "todoDescriptionTextEdit", tr("List description"));
  input->setPlainText(list->m_description);
  layout->addWidget(input);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(this, &QObject::destroyed, &dialog, &QDialog::reject);
  dialog.resize(QSize(480, 280).boundedTo(dialog.screen()->availableGeometry().size()));
  input->setFocus();
  if (dialog.exec() == QDialog::Accepted && self && accepts(token)) {
    const auto text = input->toPlainText();
    command([this, key, text]() { return m_controller->setListDescription(key, text); });
  }
}

void TodoEditor::refreshListIcon() {
  const auto *list =
      m_controller ? m_controller->getDocument().findList(m_controller->getProjection().m_listKey)
                   : nullptr;
  const auto glyph = list ? list->m_properties.value(QStringLiteral("icon")).toString() : QString();
  m_listIcon->setText(glyph);
  m_listIcon->setIcon(glyph.isEmpty() ? IconUtils::fetchIconWithDisabledState(
                                            m_services.get<ThemeService>()->getIconFile(
                                                QStringLiteral("face_slightly_smiling_plus.svg")))
                                      : QIcon());
  const auto label = glyph.isEmpty() ? tr("Add list icon") : tr("Change list icon");
  m_listIcon->setToolTip(label);
  m_listIcon->setAccessibleName(label);
}

void TodoEditor::chooseListIcon() {
  if (!canMutate()) {
    return;
  }
  const auto &projection = m_controller->getProjection();
  const auto key = projection.m_listKey;
  const auto *list = m_controller->getDocument().findList(key);
  if (!list || projection.m_view != TodoViewKind::List || !projection.m_search.isEmpty() ||
      !projection.m_tag.isEmpty()) {
    return;
  }
  const bool hasIcon = !list->m_properties.value(QStringLiteral("icon")).toString().isEmpty();
  endTyping();
  const auto token = gesture();
  const QPointer<TodoEditor> self(this);
  QPointer<QDialog> dialog = named(new QDialog(this), "todoListIconDialog", tr("List icon"));
  dialog->setWindowTitle(tr("List icon"));
  auto *layout = new QVBoxLayout(dialog);
  spacing(layout);
  auto *input =
      named(new m3::qt::EmojiLineEdit(dialog, m3::qt::EmojiLineEdit::PickerMode::Embedded),
            "todoListIconSearch", tr("Search list icons"));
  input->setPlaceholderText(tr("Search emoji names or paste emoji"));
  layout->addWidget(input);
  layout->addWidget(input->pickerWidget(), 1);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, dialog);
  auto *remove = named(buttons->addButton(tr("Remove icon"), QDialogButtonBox::DestructiveRole),
                       "todoRemoveListIcon", tr("Remove list icon"));
  remove->setEnabled(hasIcon);
  remove->setAutoDefault(false);
  layout->addWidget(buttons);
  QString chosen;
  connect(input, &m3::qt::EmojiLineEdit::emojiSelected, dialog,
          [dialog, &chosen](const QString &p_glyph) {
            chosen = p_glyph;
            dialog->accept();
          });
  connect(remove, &QPushButton::clicked, dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  dialog->resize(QSize(440, 440).boundedTo(dialog->screen()->availableGeometry().size()));
  const int result = dialog->exec();
  if (!dialog) {
    return;
  }
  delete dialog;
  if (!self || result != QDialog::Accepted || !accepts(token) ||
      m_controller->getProjection().m_view != TodoViewKind::List ||
      m_controller->getProjection().m_listKey != key) {
    return;
  }
  command([this, key, chosen]() { return m_controller->setListIcon(key, chosen); });
}

void TodoEditor::deleteList(const QString &p_key) {
  if (!canMutate()) {
    return;
  }
  const auto *list = m_controller->getDocument().findList(p_key);
  if (!list) {
    return;
  }
  endTyping();
  const auto token = gesture();
  const QPointer<TodoEditor> self(this);
  if (!list->m_items.isEmpty()) {
    const auto name = listName(*list);
    const auto answer = QMessageBox::question(this, tr("Delete list"),
                                              tr("Delete list \"%1\" and all its tasks?").arg(name),
                                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (!self || answer != QMessageBox::Yes || !accepts(token)) {
      return;
    }
  }
  command([this, p_key]() { return m_controller->removeList(p_key); });
}

void TodoEditor::deleteTask(const QString &p_key) {
  if (!canMutate()) {
    return;
  }
  const auto *item = m_controller->getDocument().findItem(p_key);
  if (!item) {
    return;
  }
  const auto name = taskName(*item);
  endTyping();
  const auto token = gesture();
  const QPointer<TodoEditor> self(this);
  const auto answer =
      QMessageBox::question(this, tr("Delete task"), tr("Delete task \"%1\"?").arg(name),
                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (self && answer == QMessageBox::Yes && accepts(token)) {
    command([this, p_key]() { return m_controller->removeTask(p_key); });
  }
}

void TodoEditor::toggleTaskMyDay(const QString &p_key) {
  if (!canMutate()) {
    return;
  }
  const auto *item = m_controller->getDocument().findItem(p_key);
  if (!item) {
    return;
  }
  const auto today = m_controller->getProjection().m_today;
  const bool inMyDay = today.isValid() && item->m_fields.m_myDay == today;
  command([this, p_key, inMyDay]() { return m_controller->setTaskInMyDay(p_key, !inMyDay); });
}

void TodoEditor::moveTask(const QString &p_key, int p_direction) {
  if (!canMutate() || !m_controller->getTaskModel()->isReorderingEnabled()) {
    return;
  }
  const TodoList *list = nullptr;
  const auto *item = m_controller->getDocument().findItem(p_key, &list);
  if (!item || !list) {
    return;
  }
  int position = -1;
  for (int i = 0; i < list->m_items.size(); ++i) {
    if (list->m_items[i].m_key == p_key) {
      position = i;
      break;
    }
  }
  const int target = position + p_direction;
  if (position < 0 || target < 0 || target >= list->m_items.size()) {
    return;
  }
  const auto listKey = list->m_key;
  const int before = p_direction < 0 ? target : target + 1;
  const QString beforeKey = before < list->m_items.size() ? list->m_items[before].m_key : QString();
  command([this, p_key, listKey, beforeKey]() {
    return m_controller->moveTask(p_key, listKey, beforeKey);
  });
}

void TodoEditor::moveList(const QString &p_key, int p_direction) {
  if (!canMutate()) {
    return;
  }
  const auto &document = m_controller->getDocument();
  const auto *list = document.findList(p_key);
  const auto *group = list ? document.groupForList(*list) : nullptr;
  if (!group) {
    return;
  }
  const auto &lists = document.m_lists;
  int position = -1;
  const int end = group->m_firstList + group->m_listCount;
  for (int i = group->m_firstList; i < end; ++i) {
    if (lists[i].m_key == p_key) {
      position = i;
      break;
    }
  }
  const int target = position + p_direction;
  if (position < 0 || target < group->m_firstList || target >= end) {
    return;
  }
  const int before = p_direction < 0 ? target : target + 1;
  const QString beforeKey = before < end ? lists[before].m_key : QString();
  const auto groupKey = group->m_key;
  command([this, p_key, groupKey, beforeKey]() {
    return m_controller->moveList(p_key, groupKey, beforeKey);
  });
}

void TodoEditor::moveListToGroup(const QString &p_key, const QString &p_groupKey) {
  if (!canMutate()) {
    return;
  }
  endTyping();
  m_groupFocused = false;
  m_controller->setView(TodoViewKind::List, p_key);
  const QPointer<TodoEditor> self(this);
  const QPointer<TodoViewWindowController> controller(m_controller);
  if (command([controller, p_key, p_groupKey]() {
        return controller->moveList(p_key, p_groupKey, {});
      }) &&
      self && m_controller == controller) {
    const auto *moved = controller->getDocument().findList(controller->getProjection().m_listKey);
    const auto *owner = moved ? controller->getDocument().groupForList(*moved) : nullptr;
    if (owner) {
      controller->setGroupExpanded(owner->m_key, true);
    }
  }
}

void TodoEditor::duplicateList(const QString &p_key) {
  const QPointer<TodoEditor> self(this);
  const QPointer<TodoViewWindowController> controller(m_controller);
  QString createdKey;
  if (command([controller, p_key, &createdKey]() {
        return controller->duplicateList(p_key, &createdKey);
      }) &&
      self && m_controller == controller) {
    const auto *copy = controller->getDocument().findList(createdKey);
    const auto *owner = copy ? controller->getDocument().groupForList(*copy) : nullptr;
    if (owner) {
      controller->setGroupExpanded(owner->m_key, true);
    }
    selectView(controller->getListModel()->rowForKey(createdKey));
    focusTasks();
  }
}

void TodoEditor::showListMenu(const QString &p_key, const QPoint &p_globalPosition) {
  const auto *list = m_controller ? m_controller->getDocument().findList(p_key) : nullptr;
  const auto *group = list ? m_controller->getDocument().groupForList(*list) : nullptr;
  if (!m_controller || !m_controller->getDocument().isValid() || (!p_key.isEmpty() && !group)) {
    return;
  }
  endTyping();
  const auto token = gesture();
  auto *menu = named(new QMenu(this), "todoListMenu", tr("List Options"));
  menu->setAttribute(Qt::WA_DeleteOnClose);
  auto add = [&](QMenu *p_menu, const QString &p_label, const char *p_name,
                 const std::function<void()> &p_action) {
    auto *action = p_menu->addAction(p_label);
    action->setObjectName(QString::fromLatin1(p_name));
    action->setEnabled(canMutate());
    connect(action, &QAction::triggered, this, [this, token, p_action]() {
      if (accepts(token)) {
        p_action();
      }
    });
    return action;
  };
  if (group) {
    add(menu, tr("Rename"), "todoRenameListAction", [this, p_key]() { renameList(p_key); });
    add(menu, tr("Edit Description"), "todoEditListDescriptionAction",
        [this, p_key]() { editListDescription(p_key); });
  }
  auto *sort = menu->addMenu(tr("Sort by"));
  sort->menuAction()->setObjectName(QStringLiteral("todoSortByAction"));
  auto *sortGroup = new QActionGroup(sort);
  sortGroup->setExclusionPolicy(QActionGroup::ExclusionPolicy::Exclusive);
  const auto addSort = [&](TodoSortOrder p_order, const QString &p_label, const char *p_name) {
    auto *action = sort->addAction(p_label);
    action->setObjectName(QString::fromLatin1(p_name));
    action->setCheckable(true);
    sortGroup->addAction(action);
    action->setChecked(m_controller->getProjection().m_sort == p_order);
    connect(action, &QAction::triggered, this, [this, token, p_key, p_order]() {
      if (!acceptsNavigation(token)) {
        return;
      }
      const QPointer<TodoEditor> self(this);
      const QPointer<TodoViewWindowController> controller(m_controller);
      if (!p_key.isEmpty()) {
        m_groupFocused = false;
        const auto &projection = controller->getProjection();
        if (projection.m_view != TodoViewKind::List || projection.m_listKey != p_key) {
          controller->setView(TodoViewKind::List, p_key);
        }
        if (!self || !controller || m_controller != controller) {
          return;
        }
        controller->setSearch({});
        if (!self || !controller || m_controller != controller) {
          return;
        }
        controller->setTagFilter({});
        if (!self || !controller || m_controller != controller) {
          return;
        }
      }
      m_services.get<ConfigMgr2>()->getEditorConfig().getTodoEditorConfig().setSortOrder(p_order);
      if (self && controller && m_controller == controller && m_narrow) {
        closeDetails();
      }
    });
  };
  addSort(TodoSortOrder::Manual, tr("Manual"), "todoSortManualAction");
  addSort(TodoSortOrder::Importance, tr("Importance"), "todoSortImportanceAction");
  addSort(TodoSortOrder::DueDate, tr("Due Date"), "todoSortDueDateAction");
  addSort(TodoSortOrder::AddedToMyDay, tr("Added to My Day"), "todoSortMyDayAction");
  addSort(TodoSortOrder::Title, tr("Name"), "todoSortNameAction");
  addSort(TodoSortOrder::CreationDate, tr("Creation Date"), "todoSortCreationDateAction");
  if (group) {
    if (group->m_isDefault) {
      auto *destinations = menu->addMenu(tr("Move List To Group"));
      destinations->menuAction()->setObjectName(QStringLiteral("todoMoveListGroupAction"));
      for (const auto &destination : m_controller->getDocument().m_groups) {
        if (!destination.m_isDefault) {
          const auto key = destination.m_key;
          auto *action = add(
              destinations, groupName(destination).replace(QLatin1Char('&'), QLatin1String("&&")),
              "todoMoveListToGroupAction", [this, p_key, key]() { moveListToGroup(p_key, key); });
          action->setData(key);
        }
      }
      destinations->menuAction()->setEnabled(canMutate() && !destinations->isEmpty());
    } else {
      add(menu, tr("Remove from Group"), "todoRemoveListFromGroupAction",
          [this, p_key]() { moveListToGroup(p_key, {}); });
    }
    add(menu, tr("Duplicate"), "todoDuplicateListAction",
        [this, p_key]() { duplicateList(p_key); });
    add(menu, tr("Delete"), "todoDeleteListAction", [this, p_key]() { deleteList(p_key); });
  }
  menu->popup(p_globalPosition);
}

void TodoEditor::showTaskMenu(const QString &p_key, const QPoint &p_globalPosition) {
  if (!m_controller || !m_controller->getDocument().findItem(p_key)) {
    return;
  }
  endTyping();
  const QPointer<TodoEditor> self(this);
  const QPointer<TodoViewWindowController> controller(m_controller);
  controller->setSelectedItemKey(p_key);
  if (!self || !controller || m_controller != controller) {
    return;
  }
  const auto token = gesture();
  const auto today = controller->getProjection().m_today;
  bool myDay = false;
  bool important = false;
  bool completed = false;
  QString ownerKey;
  {
    const auto *item = selectedItem();
    if (!item || item->m_key != p_key) {
      return;
    }
    myDay = item->m_fields.m_myDay == today;
    important = item->m_fields.m_important;
    completed = item->m_fields.m_checked;
    ownerKey = item->m_listKey;
  }
  auto *menu = named(new QMenu(this), "todoTaskMenu", tr("Task actions"));
  menu->setAttribute(Qt::WA_DeleteOnClose);
  auto *theme = m_services.get<ThemeService>();
  const auto icon = [theme](const QString &p_file) {
    return IconUtils::fetchIconWithDisabledState(theme->getIconFile(p_file));
  };
  const auto add = [&](QMenu *p_menu, const QIcon &p_icon, const QString &p_label,
                       const char *p_name, const std::function<void()> &p_action,
                       const QKeySequence &p_shortcut = QKeySequence()) {
    auto *action = p_menu->addAction(p_icon, p_label);
    action->setObjectName(QString::fromLatin1(p_name));
    action->setEnabled(canMutate());
    action->setShortcutContext(Qt::WidgetShortcut);
    action->setShortcut(p_shortcut);
    action->setShortcutVisibleInContextMenu(true);
    connect(action, &QAction::triggered, this, [this, token, p_action]() {
      if (accepts(token)) {
        p_action();
      }
    });
    return action;
  };
  add(
      menu, icon(QStringLiteral("sun.svg")), myDay ? tr("Remove from My Day") : tr("Add to My Day"),
      "todoToggleMyDayAction",
      [this, p_key, myDay]() {
        // Keep the offered add/remove intent if today changes while the menu is open.
        command([this, p_key, myDay]() { return m_controller->setTaskInMyDay(p_key, !myDay); });
      },
      QKeySequence(Qt::CTRL | Qt::Key_T));
  add(menu, icon(QStringLiteral("star.svg")),
      important ? tr("Remove importance") : tr("Mark as important"), "todoToggleImportantAction",
      [this]() {
        updateTask([](TodoTaskFields &p_fields) { p_fields.m_important = !p_fields.m_important; });
      });
  add(
      menu, icon(QStringLiteral("circle_check.svg")),
      completed ? tr("Mark as not completed") : tr("Mark as completed"),
      "todoToggleCompletedAction",
      [this]() {
        updateTask([](TodoTaskFields &p_fields) { p_fields.m_checked = !p_fields.m_checked; });
      },
      QKeySequence(Qt::CTRL | Qt::Key_D));
  menu->addSeparator();
  add(menu, icon(QStringLiteral("calendar.svg")), tr("Due today"), "todoDueTodayAction",
      [this, today]() { updateTask([&](TodoTaskFields &p_fields) { p_fields.m_due = today; }); });
  add(menu, icon(QStringLiteral("calendar_arrow_right.svg")), tr("Due tomorrow"),
      "todoDueTomorrowAction", [this, tomorrow = today.addDays(1)]() {
        updateTask([&](TodoTaskFields &p_fields) { p_fields.m_due = tomorrow; });
      });
  add(menu, icon(QStringLiteral("calendar_days.svg")), tr("Pick a date"), "todoPickDueDateAction",
      [this]() { pickDueDate(); });
  menu->addSeparator();
  auto *destinations = menu->addMenu(icon(QStringLiteral("move.svg")), tr("Move task to..."));
  destinations->menuAction()->setObjectName(QStringLiteral("todoMoveTaskAction"));
  for (const auto &list : m_controller->getDocument().m_lists) {
    if (list.m_key == ownerKey) {
      continue;
    }
    const auto key = list.m_key;
    auto *action =
        add(destinations, {},
            qualifiedListName(m_controller->getDocument(), list)
                .replace(QLatin1Char('&'), QLatin1String("&&")),
            "todoMoveTaskToListAction", [this, p_key, key]() {
              command([this, p_key, key]() { return m_controller->moveTask(p_key, key, {}); });
            });
    action->setData(key);
  }
  destinations->menuAction()->setEnabled(canMutate() && !destinations->isEmpty());
  menu->addSeparator();
  const auto deleteIcon = IconUtils::fetchIcon(
      theme->getIconFile(QStringLiteral("delete.svg")),
      QVector<IconUtils::OverriddenColor>{
          {theme->paletteColor(QStringLiteral("base#error#fg")), QIcon::Normal},
          {theme->paletteColor(QStringLiteral("base#disabled#fg")), QIcon::Disabled}});
  add(
      menu, deleteIcon, tr("Delete task"), "todoDeleteTaskAction",
      [this, p_key]() { deleteTask(p_key); }, QKeySequence(Qt::Key_Delete));
  menu->popup(p_globalPosition);
}

void TodoEditor::showTextMenu(QWidget *p_widget, const QPoint &p_position) {
  QPointer<QLineEdit> line = qobject_cast<QLineEdit *>(p_widget);
  QPointer<QTextEdit> text = qobject_cast<QTextEdit *>(p_widget);
  if (!line && !text) {
    return;
  }
  endTyping();
  const auto token = gesture();
  const bool projected = line == m_title || line == m_listTitle ||
                         m_steps->isAncestorOf(p_widget) || text == m_note->getTextEdit();
  auto *menu = new QMenu(p_widget);
  menu->setAttribute(Qt::WA_DeleteOnClose);
  auto *undo = menu->addAction(tr("Undo"));
  undo->setObjectName(QStringLiteral("todoNativeUndoAction"));
  const bool localHistory =
      line == m_listTitle && (line->isUndoAvailable() || line->isRedoAvailable());
  undo->setEnabled(canMutate() && (!localHistory || line->isUndoAvailable()));
  connect(undo, &QAction::triggered, this, [this, line, localHistory]() {
    if (canMutate()) {
      if (localHistory) {
        if (line) {
          line->undo();
        }
      } else {
        emit undoRequested();
      }
    }
  });
  auto *redo = menu->addAction(tr("Redo"));
  redo->setObjectName(QStringLiteral("todoNativeRedoAction"));
  redo->setEnabled(canMutate() && (!localHistory || line->isRedoAvailable()));
  connect(redo, &QAction::triggered, this, [this, line, localHistory]() {
    if (canMutate()) {
      if (localHistory) {
        if (line) {
          line->redo();
        }
      } else {
        emit redoRequested();
      }
    }
  });
  menu->addSeparator();
  const bool readOnly = line ? line->isReadOnly() : text->isReadOnly();
  const bool selected = line ? line->hasSelectedText() : text->textCursor().hasSelection();
  auto *cut = menu->addAction(tr("Cut"));
  cut->setEnabled(!readOnly && selected);
  connect(cut, &QAction::triggered, this, [this, line, text, token, projected]() {
    if (projected && !accepts(token)) {
      return;
    }
    const QPointer<TodoEditor> self(this);
    if (line && line->isEnabled() && !line->isReadOnly()) {
      line->cut();
    } else if (text && text->isEnabled() && !text->isReadOnly()) {
      text->cut();
    }
    if (self) {
      endTyping();
    }
  });
  auto *copy = menu->addAction(tr("Copy"));
  copy->setEnabled(selected);
  connect(copy, &QAction::triggered, this, [line, text]() {
    if (line) {
      line->copy();
    } else if (text) {
      text->copy();
    }
  });
  auto *paste = menu->addAction(tr("Paste"));
  paste->setEnabled(!readOnly);
  connect(paste, &QAction::triggered, this, [this, line, text, token, projected]() {
    if (projected && !accepts(token)) {
      return;
    }
    const QPointer<TodoEditor> self(this);
    if (line && line->isEnabled() && !line->isReadOnly()) {
      line->paste();
    } else if (text && text->isEnabled() && !text->isReadOnly()) {
      text->paste();
    }
    if (self) {
      endTyping();
    }
  });
  menu->addSeparator();
  auto *select = menu->addAction(tr("Select All"));
  connect(select, &QAction::triggered, this, [line, text]() {
    if (line) {
      line->selectAll();
    } else if (text) {
      text->selectAll();
    }
  });
  menu->popup(p_position);
}

bool TodoEditor::eventFilter(QObject *p_object, QEvent *p_event) {
  auto *widget = qobject_cast<QWidget *>(p_object);
  if (widget &&
      (p_event->type() == QEvent::ShortcutOverride || p_event->type() == QEvent::KeyPress)) {
    auto *menu = qobject_cast<QMenu *>(widget);
    auto *taskMenu = menu;
    if (auto *parentMenu = menu ? qobject_cast<QMenu *>(menu->parentWidget()) : nullptr) {
      taskMenu = parentMenu;
    }
    if (taskMenu && taskMenu->parentWidget() == this &&
        taskMenu->objectName() == QLatin1String("todoTaskMenu") &&
        QApplication::activePopupWidget() == menu) {
      auto *key = static_cast<QKeyEvent *>(p_event);
      const QKeySequence shortcut(key->keyCombination());
      for (auto *action : taskMenu->actions()) {
        if (action->shortcut().isEmpty() || action->shortcut() != shortcut) {
          continue;
        }
        // Standalone QMenu actions have no widget association for Qt's shortcut matcher.
        // Dispatch their real shortcuts locally, ahead of the host's EditRead shortcut.
        key->accept();
        if (p_event->type() == QEvent::KeyPress && action->isEnabled()) {
          const QPointer<TodoEditor> self(this);
          const QPointer<QMenu> guardedMenu(taskMenu);
          const QPointer<QAction> guardedAction(action);
          if (menu != taskMenu) {
            menu->close();
          }
          if (guardedMenu) {
            guardedMenu->close();
          }
          if (self && guardedAction) {
            guardedAction->trigger();
          }
        }
        return true;
      }
    }
  }
  if (m_narrow && m_listsButton->isChecked()) {
    if (widget == m_splitter &&
        (p_event->type() == QEvent::Resize || p_event->type() == QEvent::Move)) {
      updateSidebarGeometry();
    }
    // Host controls dismiss the overlay; owned popup/dialog windows retain their input.
    if (widget && widget->window() == window() && widget != m_listsButton && widget != m_sidebar &&
        !m_sidebar->isAncestorOf(widget) &&
        (p_event->type() == QEvent::MouseButtonPress || p_event->type() == QEvent::FocusIn)) {
      setSidebarOpen(false);
    }
  }
  if (!widget || (widget != this && !isAncestorOf(widget)) || widget->window() != window()) {
    return QWidget::eventFilter(p_object, p_event);
  }
  if (p_object == m_tasks->viewport() && (p_event->type() == QEvent::MouseButtonPress ||
                                          p_event->type() == QEvent::MouseButtonDblClick)) {
    const auto *mouse = static_cast<QMouseEvent *>(p_event);
    // Selection refreshes details during the press, before taskClicked is emitted.
    // Let Qt handle double-clicks, but never reuse the first click's close intent.
    m_closeDetailsOnTaskClick =
        p_event->type() == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton &&
        m_detailsOpen &&
        m_tasks->indexAt(mouse->position().toPoint()).data(TodoTaskModel::IdRole).toString() ==
            m_detailsKey;
  }
  if (p_event->type() == QEvent::FocusOut && widget == m_typingField) {
    endTyping();
  }
  if (p_event->type() == QEvent::Hide && widget == this) {
    setSidebarOpen(false);
    endTyping();
  }
  if (widget == m_search) {
    if (p_event->type() == QEvent::InputMethod) {
      m_searchComposing = !static_cast<QInputMethodEvent *>(p_event)->preeditString().isEmpty();
    } else if (p_event->type() == QEvent::FocusOut) {
      m_searchComposing = false;
    }
  }
  if (p_event->type() == QEvent::MouseButtonPress && m_typingField &&
      (widget == m_typingField || m_typingField->isAncestorOf(widget))) {
    endTyping();
  }
  if (p_event->type() == QEvent::ContextMenu) {
    QWidget *target = widget;
    if (!qobject_cast<QLineEdit *>(target) && !qobject_cast<QTextEdit *>(target)) {
      target = qobject_cast<QTextEdit *>(widget->parentWidget());
    }
    if (target && (qobject_cast<QLineEdit *>(target) || qobject_cast<QTextEdit *>(target))) {
      showTextMenu(target, static_cast<QContextMenuEvent *>(p_event)->globalPos());
      p_event->accept();
      return true;
    }
  }
  if (p_event->type() != QEvent::ShortcutOverride && p_event->type() != QEvent::KeyPress) {
    return QWidget::eventFilter(p_object, p_event);
  }
  auto *key = static_cast<QKeyEvent *>(p_event);
  if (m_narrow && m_listsButton->isChecked() &&
      (key->key() == Qt::Key_Escape ||
       (widget == m_search && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)))) {
    if (m_searchComposing) {
      return QWidget::eventFilter(p_object, p_event);
    }
    key->accept();
    if (p_event->type() == QEvent::KeyPress) {
      if (key->key() == Qt::Key_Escape) {
        setSidebarOpen(false);
      } else {
        focusTasks();
      }
    }
    return true;
  }
  if (widget == m_tasks && !QApplication::activePopupWidget() &&
      !QApplication::activeModalWidget()) {
    const bool myDay = key->modifiers() == Qt::ControlModifier && key->key() == Qt::Key_T;
    const bool completed = key->modifiers() == Qt::ControlModifier && key->key() == Qt::Key_D;
    const bool remove = key->modifiers() == Qt::NoModifier && key->key() == Qt::Key_Delete;
    if (myDay || completed || remove) {
      key->accept();
      if (p_event->type() == QEvent::KeyPress && canMutate()) {
        const auto index = m_tasks->currentIndex();
        const auto id = index.data(TodoTaskModel::IdRole).toString();
        if (index.isValid() && !index.data(TodoTaskModel::SectionRole).toBool() && !id.isEmpty() &&
            m_controller->getDocument().findItem(id)) {
          if (myDay) {
            toggleTaskMyDay(id);
          } else if (completed) {
            endTyping();
            m_controller->getTaskModel()->setData(index,
                                                  !index.data(TodoTaskModel::CheckedRole).toBool(),
                                                  TodoTaskModel::CheckedRole);
          } else {
            deleteTask(id);
          }
        }
      }
      return true;
    }
  }
  const bool undo = key->matches(QKeySequence::Undo);
  const bool redo = key->matches(QKeySequence::Redo);
  const bool find = key->matches(QKeySequence::Find);
  if (undo || redo || find) {
    key->accept();
    if (p_event->type() == QEvent::KeyPress) {
      endTyping();
      if (find) {
        focusSearch();
      } else if (canMutate()) {
        if (widget == m_listTitle &&
            (m_listTitle->isUndoAvailable() || m_listTitle->isRedoAvailable())) {
          if (undo) {
            m_listTitle->undo();
          } else {
            m_listTitle->redo();
          }
        } else if (undo) {
          emit undoRequested();
        } else {
          emit redoRequested();
        }
      }
    }
    return true;
  }
  if (p_event->type() != QEvent::KeyPress) {
    return QWidget::eventFilter(p_object, p_event);
  }
  if (key->key() == Qt::Key_Escape && m_detailsOpen) {
    closeDetails();
    key->accept();
    return true;
  }
  if (key->matches(QKeySequence::SelectAll) || key->matches(QKeySequence::Cut) ||
      key->matches(QKeySequence::Paste) || key->key() == Qt::Key_Left ||
      key->key() == Qt::Key_Right || key->key() == Qt::Key_Up || key->key() == Qt::Key_Down ||
      key->key() == Qt::Key_Home || key->key() == Qt::Key_End || key->key() == Qt::Key_PageUp ||
      key->key() == Qt::Key_PageDown) {
    endTyping();
  }
  if (widget == m_tasks && m_controller) {
    const auto index = m_tasks->currentIndex();
    const auto id = index.data(TodoTaskModel::IdRole).toString();
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter ||
        key->key() == Qt::Key_Space) {
      if (index.data(TodoTaskModel::SectionRole).toBool()) {
        m_controller->setCompletedExpanded(!index.data(TodoTaskModel::ExpandedRole).toBool());
      } else if (key->key() == Qt::Key_Space) {
        endTyping();
        m_controller->getTaskModel()->setData(
            index, !index.data(TodoTaskModel::CheckedRole).toBool(), TodoTaskModel::CheckedRole);
      } else {
        openTask(index);
        if (m_detailsOpen) {
          m_title->setFocus(Qt::OtherFocusReason);
        }
      }
      key->accept();
      return true;
    }
    if (key->modifiers() == Qt::AltModifier &&
        (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down)) {
      moveTask(id, key->key() == Qt::Key_Up ? -1 : 1);
      return true;
    }
  }
  if (m_controller && widget == m_lists) {
    const bool groupRow = m_lists->currentIndex().data(TodoListModel::GroupRole).toBool();
    const auto groupKey = m_lists->currentIndex().data(TodoListModel::GroupIdRole).toString();
    if (key->modifiers() == Qt::AltModifier &&
        (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down)) {
      if (groupRow) {
        moveGroup(groupKey, key->key() == Qt::Key_Up ? -1 : 1);
      } else {
        moveList(m_lists->currentIndex().data(TodoListModel::IdRole).toString(),
                 key->key() == Qt::Key_Up ? -1 : 1);
      }
      return true;
    }
    if (groupRow && !groupKey.isEmpty() && key->modifiers() == Qt::NoModifier) {
      if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter ||
          key->key() == Qt::Key_Space) {
        toggleGroup(groupKey);
        return true;
      }
      if (key->key() == Qt::Key_Left || key->key() == Qt::Key_Right) {
        endTyping();
        m_groupFocused = true;
        m_controller->setSelectedGroupKey(groupKey);
        m_controller->setGroupExpanded(groupKey, key->key() == Qt::Key_Right);
        return true;
      }
    }
  }
  return QWidget::eventFilter(p_object, p_event);
}

void TodoEditor::scroll(bool p_up) {
  QScrollBar *bar =
      m_detailsOpen && (m_narrow || m_details->isAncestorOf(QApplication::focusWidget()))
          ? m_detailsScroll->verticalScrollBar()
          : m_tasks->verticalScrollBar();
  bar->triggerAction(p_up ? QAbstractSlider::SliderPageStepSub
                          : QAbstractSlider::SliderPageStepAdd);
}

QString TodoEditor::selectedText() const {
  auto *focused = QApplication::focusWidget();
  if (!focused || !isAncestorOf(focused)) {
    return {};
  }
  if (const auto *line = qobject_cast<const QLineEdit *>(focused)) {
    return line->selectedText();
  }
  if (const auto *text = qobject_cast<const vte::VTextEdit *>(focused)) {
    return text->selectedText();
  }
  if (const auto *text = qobject_cast<const QTextEdit *>(focused)) {
    return text->textCursor().selectedText();
  }
  return {};
}

void TodoEditor::invalidateGestures() {
  endTyping();
  m_closeDetailsOnTaskClick = false;
  ++m_generation;
}

void TodoEditor::resetForBuffer() {
  setSidebarOpen(false);
  invalidateGestures();
  m_detailsOpen = false;
  m_groupFocused = false;
  m_detailsKey.clear();
  m_lastDestination.clear();
  {
    QScopedValueRollback<bool> updating(m_updating, true);
    m_addTask->clear();
    m_addStep->clear();
    m_addHasDue->setChecked(false);
    m_addDue->setDate(QDate::currentDate());
  }
  refresh();
}
