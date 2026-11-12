#include "todoeditor.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>
#include <functional>

#include <controllers/todoviewwindowcontroller.h>
#include <core/servicelocator.h>
#include <gui/services/themeservice.h>
#include <gui/utils/iconutils.h>
#include <models/todomodels.h>
#include <vtextedit/vtextedit.h>

#include "simplemarkdowneditor.h"

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
  return group ? TodoEditor::tr("%1 / %2").arg(groupName(*group), listName(p_list))
               : listName(p_list);
}

void addGroupChoices(QComboBox *p_combo, const TodoDocument &p_document) {
  const auto *defaultGroup = p_document.defaultGroup();
  p_combo->addItem(defaultGroup ? groupName(*defaultGroup) : TodoEditor::tr("Default"), QString());
  for (const auto &group : p_document.m_groups) {
    if (!group.m_isDefault) {
      p_combo->addItem(groupName(group), group.m_key);
    }
  }
}

class TaskViewProxy : public QSortFilterProxyModel {
public:
  using QSortFilterProxyModel::QSortFilterProxyModel;
  QVariant data(const QModelIndex &p_index, int p_role = Qt::DisplayRole) const override {
    if (p_role == Qt::DisplayRole &&
        !QSortFilterProxyModel::data(p_index, TodoListModel::GroupIdRole).toString().isEmpty()) {
      return TodoEditor::tr("%1 / %2").arg(
          QSortFilterProxyModel::data(p_index, TodoListModel::GroupTitleRole).toString(),
          QSortFilterProxyModel::data(p_index, TodoListModel::TitleRole).toString());
    }
    return QSortFilterProxyModel::data(p_index, p_role);
  }

protected:
  bool filterAcceptsRow(int p_row, const QModelIndex &p_parent) const override {
    return !sourceModel()->index(p_row, 0, p_parent).data(TodoListModel::GroupRole).toBool();
  }
};

QString taskName(const TodoItem &p_item) {
  return p_item.m_fields.m_title.trimmed().isEmpty() ? TodoEditor::tr("Untitled task")
                                                     : p_item.m_fields.m_title;
}

class ListDelegate : public QStyledItemDelegate {
public:
  ListDelegate(ThemeService *p_theme, QListView *p_view) : QStyledItemDelegate(p_view) {
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
    p_painter->save();
    p_painter->translate(icon.topLeft());
    p_painter->scale(side / 24.0, side / 24.0);
    p_painter->setBrush(Qt::NoBrush);
    p_painter->setPen(
        QPen(selected || !enabled || group ? foreground : m_iconColors[static_cast<int>(view)], 1.5,
             Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p_painter->drawPath(group ? m_groupIcon : m_icons[static_cast<int>(view)]);
    p_painter->restore();

    int trailing = row.right() - 12;
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

class TaskDelegate : public QStyledItemDelegate {
public:
  TaskDelegate(ServiceLocator &p_services, QObject *p_parent)
      : QStyledItemDelegate(p_parent), m_theme(p_services.get<ThemeService>()) {
    Q_ASSERT(m_theme);
  }

  static QRect checkRect(const QStyleOptionViewItem &p_option) {
    const int side = qMax(20, p_option.fontMetrics.height());
    return QRect(p_option.rect.left() + 8, p_option.rect.center().y() - side / 2, side, side);
  }
  static QRect starRect(const QStyleOptionViewItem &p_option) {
    const int side = qMax(20, p_option.fontMetrics.height());
    return QRect(p_option.rect.right() - side - 8, p_option.rect.center().y() - side / 2, side,
                 side);
  }

  void paint(QPainter *p_painter, const QStyleOptionViewItem &p_option,
             const QModelIndex &p_index) const override {
    QStyleOptionViewItem option(p_option);
    initStyleOption(&option, p_index);
    const auto *widget = option.widget;
    const auto *style = widget ? widget->style() : QApplication::style();
    option.text.clear();
    option.features &= ~QStyleOptionViewItem::HasCheckIndicator;
    style->drawControl(QStyle::CE_ItemViewItem, &option, p_painter, widget);
    p_painter->save();
    p_painter->setClipRect(option.rect);
    p_painter->setRenderHint(QPainter::Antialiasing);
    const auto group =
        (option.state & QStyle::State_Enabled) ? QPalette::Active : QPalette::Disabled;
    const bool selected = option.state & QStyle::State_Selected;
    // QSS paints the item background, but does not rewrite this option's
    // HighlightedText. Use the same theme tokens as QListView::item:selected.
    const auto foreground =
        selected ? (option.state & QStyle::State_Active
                        ? QStringLiteral("widgets#qlistview#item#selected#active#fg")
                        : QStringLiteral("widgets#qlistview#item#selected#inactive#fg"))
                 : (option.state & QStyle::State_MouseOver
                        ? QStringLiteral("widgets#qlistview#item#hover#fg")
                        : QStringLiteral("widgets#qlistview#fg"));
    const QColor textColor = group == QPalette::Disabled
                                 ? option.palette.color(group, QPalette::Text)
                                 : QColor(m_theme->paletteColor(foreground));
    p_painter->setPen(textColor);
    p_painter->setFont(option.font);
    const QRect circle = checkRect(option);
    if (p_index.data(TodoTaskModel::SectionRole).toBool()) {
      QStyleOption arrow;
      arrow.rect = circle.adjusted(4, 4, -4, -4);
      arrow.palette = option.palette;
      arrow.state = option.state;
      style->drawPrimitive(p_index.data(TodoTaskModel::ExpandedRole).toBool()
                               ? QStyle::PE_IndicatorArrowDown
                               : QStyle::PE_IndicatorArrowRight,
                           &arrow, p_painter, widget);
      p_painter->drawText(option.rect.adjusted(circle.width() + 20, 0, -8, 0),
                          Qt::AlignVCenter | Qt::AlignLeft, p_index.data().toString());
      p_painter->restore();
      return;
    }

    const bool checked = p_index.data(TodoTaskModel::CheckedRole).toBool();
    p_painter->setPen(QPen(textColor, 1.5));
    p_painter->setBrush(checked ? QBrush(option.palette.color(group, QPalette::Highlight))
                                : Qt::NoBrush);
    p_painter->drawEllipse(circle.adjusted(2, 2, -2, -2));
    if (checked) {
      p_painter->setPen(QPen(option.palette.color(group, QPalette::HighlightedText), 2));
      const QPoint middle(circle.left() + circle.width() * 4 / 10,
                          circle.top() + circle.height() * 7 / 10);
      p_painter->drawLine(QPoint(circle.left() + circle.width() / 4, circle.center().y()), middle);
      p_painter->drawLine(
          middle, QPoint(circle.right() - circle.width() / 4, circle.top() + circle.height() / 3));
    }
    const QRect star = starRect(option);
    QPainterPath path;
    constexpr double pi = 3.14159265358979323846;
    for (int i = 0; i < 10; ++i) {
      const double angle = -pi / 2 + i * pi / 5;
      const double radius = star.width() * (i % 2 ? 0.20 : 0.46);
      const QPointF point(star.center().x() + std::cos(angle) * radius,
                          star.center().y() + std::sin(angle) * radius);
      if (i == 0) {
        path.moveTo(point);
      } else {
        path.lineTo(point);
      }
    }
    path.closeSubpath();
    p_painter->setPen(QPen(textColor, 1.5));
    p_painter->setBrush(p_index.data(TodoTaskModel::ImportantRole).toBool() ? QBrush(textColor)
                                                                            : Qt::NoBrush);
    p_painter->drawPath(path);

    const QRect textRect(circle.right() + 8, option.rect.top() + 4,
                         qMax(0, star.left() - circle.right() - 16), option.rect.height() - 8);
    auto titleFont = option.font;
    titleFont.setStrikeOut(checked);
    p_painter->setFont(titleFont);
    p_painter->setPen(textColor);
    p_painter->drawText(textRect, Qt::AlignLeft | Qt::AlignTop,
                        QFontMetrics(titleFont).elidedText(p_index.data().toString(),
                                                           Qt::ElideRight, textRect.width()));
    QStringList summaries;
    summaries.append(
        TodoEditor::tr("%1 / %2").arg(p_index.data(TodoTaskModel::SourceGroupNameRole).toString(),
                                      p_index.data(TodoTaskModel::SourceListNameRole).toString()));
    const auto due = p_index.data(TodoTaskModel::DueRole).toDate();
    if (due.isValid()) {
      summaries.append(TodoEditor::tr("Due %1").arg(QLocale().toString(due, QLocale::ShortFormat)));
    }
    const auto myDay = p_index.data(TodoTaskModel::MyDayRole).toDate();
    if (myDay.isValid()) {
      summaries.append(
          TodoEditor::tr("My Day: %1").arg(QLocale().toString(myDay, QLocale::ShortFormat)));
    }
    const int steps = p_index.data(TodoTaskModel::StepsTotalRole).toInt();
    if (steps) {
      summaries.append(TodoEditor::tr("%1/%2 steps")
                           .arg(p_index.data(TodoTaskModel::StepsCompletedRole).toInt())
                           .arg(steps));
    }
    if (!p_index.data(TodoTaskModel::NoteRole).toString().isEmpty()) {
      summaries.append(TodoEditor::tr("Note"));
    }
    summaries.append(p_index.data(TodoTaskModel::TagsRole).toStringList());
    auto secondaryFont = option.font;
    if (secondaryFont.pointSizeF() > 2) {
      secondaryFont.setPointSizeF(secondaryFont.pointSizeF() - 1);
    }
    p_painter->setFont(secondaryFont);
    p_painter->setPen(selected ? textColor
                               : QColor(m_theme->paletteColor(QStringLiteral("base#muted#fg"))));
    p_painter->drawText(
        textRect, Qt::AlignLeft | Qt::AlignBottom,
        QFontMetrics(secondaryFont)
            .elidedText(summaries.join(QStringLiteral(" / ")), Qt::ElideRight, textRect.width()));
    p_painter->restore();
  }

  QSize sizeHint(const QStyleOptionViewItem &p_option, const QModelIndex &p_index) const override {
    return QSize(160, p_option.fontMetrics.height() *
                              (p_index.data(TodoTaskModel::SectionRole).toBool() ? 1 : 2) +
                          16);
  }

  bool editorEvent(QEvent *p_event, QAbstractItemModel *p_model,
                   const QStyleOptionViewItem &p_option, const QModelIndex &p_index) override {
    if (p_event->type() != QEvent::MouseButtonRelease) {
      return false;
    }
    const auto *event = static_cast<QMouseEvent *>(p_event);
    if (event->button() != Qt::LeftButton) {
      return false;
    }
    if (p_index.data(TodoTaskModel::SectionRole).toBool()) {
      return p_model->setData(p_index, !p_index.data(TodoTaskModel::ExpandedRole).toBool(),
                              TodoTaskModel::ExpandedRole);
    }
    if (!(p_index.flags() & Qt::ItemIsUserCheckable)) {
      return false;
    }
    if (checkRect(p_option).contains(event->position().toPoint())) {
      return p_model->setData(p_index, !p_index.data(TodoTaskModel::CheckedRole).toBool(),
                              TodoTaskModel::CheckedRole);
    }
    if (starRect(p_option).contains(event->position().toPoint())) {
      return p_model->setData(p_index, !p_index.data(TodoTaskModel::ImportantRole).toBool(),
                              TodoTaskModel::ImportantRole);
    }
    return false;
  }

private:
  ThemeService *m_theme;
};
} // namespace

TodoEditor::TodoEditor(ServiceLocator &p_services, QWidget *p_parent)
    : QWidget(p_parent), m_services(p_services) {
  named(this, "todoEditor", tr("TODO planner"));
  createUi();
  qApp->installEventFilter(this);
  refresh();
}

TodoEditor::~TodoEditor() { qApp->removeEventFilter(this); }

void TodoEditor::createUi() {
  auto *root = new QVBoxLayout(this);
  spacing(root);
  root->setContentsMargins(4, 4, 4, 4);
  m_navigation = named(new QWidget(this), "todoNarrowNavigation", tr("List navigation"));
  auto *navigation = new QVBoxLayout(m_navigation);
  spacing(navigation);
  m_viewCombo = named(new QComboBox(m_navigation), "todoViewCombo", tr("List or smart view"));
  m_viewCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_viewCombo->setMinimumContentsLength(8);
  m_viewProxy = new TaskViewProxy(this);
  m_viewCombo->setModel(m_viewProxy);
  navigation->addWidget(m_viewCombo);
  root->addWidget(m_navigation);

  m_splitter = named(new QSplitter(Qt::Horizontal, this), "todoSplitter", tr("Planner panes"));
  m_splitter->setChildrenCollapsible(false);
  m_splitter->setHandleWidth(4);
  root->addWidget(m_splitter, 1);
  m_sidebar = named(new QWidget(m_splitter), "todoSidebar", tr("Lists and search"));
  auto *sidebar = new QVBoxLayout(m_sidebar);
  spacing(sidebar);
  m_searchArea = new QWidget(m_sidebar);
  auto *searchArea = new QVBoxLayout(m_searchArea);
  searchArea->setContentsMargins(8, 4, 8, 4);
  searchArea->setSpacing(4);
  m_search = named(new QLineEdit(m_searchArea), "todoSearch", tr("Search tasks in this file"));
  m_search->setPlaceholderText(tr("Search tasks"));
  m_search->setClearButtonEnabled(true);
  auto *searchAction = m_search->addAction(QIcon(), QLineEdit::TrailingPosition);
  searchAction->setText(tr("Search tasks"));
  connect(searchAction, &QAction::triggered, this, &TodoEditor::focusSearch);
  connect(m_search, &QLineEdit::textChanged, searchAction,
          [searchAction](const QString &p_text) { searchAction->setVisible(p_text.isEmpty()); });
  searchArea->addWidget(m_search);
  m_groupCombo = named(new QComboBox(m_searchArea), "todoGroupCombo", tr("Organizational group"));
  m_groupCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_groupCombo->setMinimumContentsLength(8);
  searchArea->addWidget(m_groupCombo);
  m_groupMore = named(new QPushButton(tr("Group actions"), m_searchArea), "todoGroupMore",
                      tr("Group actions"));
  searchArea->addWidget(m_groupMore);
  m_groupCombo->hide();
  m_groupMore->hide();
  sidebar->addWidget(m_searchArea);
  auto *theme = m_services.get<ThemeService>();
  Q_ASSERT(theme);
  m_lists = named(new QListView(m_sidebar), "todoLists", tr("Lists and smart views"));
  m_lists->setItemDelegate(new ListDelegate(theme, m_lists));
  m_lists->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_lists->setContextMenuPolicy(Qt::CustomContextMenu);
  m_lists->setMouseTracking(true);
  m_lists->setFrameShape(QFrame::NoFrame);
  m_lists->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_lists->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_lists->setStyleSheet(
      QStringLiteral("QListView#todoLists { background: transparent; border: none; }"));
  sidebar->addWidget(m_lists, 1);
  m_creationArea = named(new QWidget(m_sidebar), "todoCreationArea", tr("Create lists and groups"));
  auto *creation = new QHBoxLayout(m_creationArea);
  creation->setContentsMargins(8, 4, 8, 4);
  creation->setSpacing(4);
  m_newList = named(new QPushButton(tr("New List"), m_creationArea), "todoNewList", tr("New list"));
  m_newGroup = named(new QPushButton(m_creationArea), "todoNewGroup", tr("New group"));
  m_newGroup->setToolTip(tr("New group"));
  m_newGroup->setFixedWidth(40);
  for (auto *button : {m_newList, m_newGroup}) {
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
  sidebar->addWidget(m_creationArea);
  const auto refreshIcons = [this, theme, searchAction]() {
    searchAction->setIcon(IconUtils::fetchIcon(theme->getIconFile(QStringLiteral("search.svg"))));
    m_newList->setIcon(
        IconUtils::fetchIconWithDisabledState(theme->getIconFile(QStringLiteral("add.svg"))));
    m_newGroup->setIcon(
        IconUtils::fetchIconWithDisabledState(theme->getIconFile(QStringLiteral("new_group.svg"))));
  };
  refreshIcons();
  connect(theme, &ThemeService::themeChanged, this, refreshIcons);

  m_pages = named(new QStackedWidget(m_splitter), "todoNativePages", tr("Tasks or details"));
  m_taskPane = named(new QWidget(m_pages), "todoTaskPane", tr("Tasks"));
  m_pages->addWidget(m_taskPane);
  auto *tasksLayout = new QVBoxLayout(m_taskPane);
  spacing(tasksLayout);
  auto *heading = new QHBoxLayout;
  heading->setSpacing(4);
  m_listTitle = named(new QLineEdit(m_taskPane), "todoListTitle", tr("List title"));
  m_listTitle->setPlaceholderText(tr("Untitled list"));
  m_viewTitle = named(new QLabel(m_taskPane), "todoViewTitle", tr("Current view"));
  heading->addWidget(m_listTitle, 1);
  heading->addWidget(m_viewTitle, 1);
  m_listMore =
      named(new QPushButton(tr("List actions"), m_taskPane), "todoListMore", tr("List actions"));
  heading->addWidget(m_listMore);
  tasksLayout->addLayout(heading);
  m_description = named(new SimpleMarkdownEditor(m_services, m_taskPane), "todoListDescription",
                        tr("List description in Markdown"));
  named(m_description->getTextEdit(), "todoDescriptionTextEdit",
        tr("List description in Markdown"));
  m_description->getTextEdit()->setUndoRedoEnabled(false);
  m_description->setMinimumHeight(60);
  m_description->setMaximumHeight(104);
  tasksLayout->addWidget(m_description);
  auto *tools = new QHBoxLayout;
  tools->setSpacing(4);
  m_sort = named(new QComboBox(m_taskPane), "todoSort", tr("Sort tasks"));
  m_sort->addItem(tr("Manual order"), static_cast<int>(TodoSortOrder::Manual));
  m_sort->addItem(tr("Importance"), static_cast<int>(TodoSortOrder::Importance));
  m_sort->addItem(tr("Due date"), static_cast<int>(TodoSortOrder::DueDate));
  m_sort->addItem(tr("Title"), static_cast<int>(TodoSortOrder::Title));
  tools->addWidget(m_sort);
  m_tagFilter = named(new QPushButton(m_taskPane), "todoTagFilter", tr("Clear exact tag filter"));
  tools->addWidget(m_tagFilter);
  tools->addStretch();
  tasksLayout->addLayout(tools);
  m_tasks = named(new QListView(m_taskPane), "todoTasks",
                  tr("Tasks; Enter opens details, Space completes"));
  m_tasks->setItemDelegate(new TaskDelegate(m_services, m_tasks));
  m_tasks->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_tasks->setSelectionMode(QAbstractItemView::SingleSelection);
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
  add->setSpacing(4);
  m_addTask = named(new QLineEdit(m_taskPane), "todoAddTask", tr("Add a task"));
  m_addTask->setPlaceholderText(tr("Add a task"));
  m_addButton = named(new QPushButton(tr("Add"), m_taskPane), "todoAddButton", tr("Add task"));
  add->addWidget(m_addTask, 1);
  add->addWidget(m_addButton);
  tasksLayout->addLayout(add);

  createDetails();
  m_splitter->setStretchFactor(0, 0);
  m_splitter->setStretchFactor(1, 1);
  m_splitter->setStretchFactor(2, 0);
  m_splitter->setSizes({260, 480, 320});
  m_navigation->hide();

  connect(m_search, &QLineEdit::textChanged, this, [this](const QString &p_text) {
    if (!m_updating && m_controller) {
      endTyping();
      m_controller->setSearch(p_text);
    }
  });
  connect(m_newList, &QPushButton::clicked, this, [this]() { addList(); });
  connect(m_newGroup, &QPushButton::clicked, this, &TodoEditor::addGroup);
  connect(m_groupCombo, &QComboBox::activated, this, [this](int p_row) {
    if (!m_updating && m_controller && p_row >= 0) {
      endTyping();
      m_groupFocused = true;
      m_controller->setSelectedGroupKey(m_groupCombo->itemData(p_row).toString());
      refresh();
    }
  });
  connect(m_groupMore, &QPushButton::clicked, this, [this]() {
    showGroupMenu(selectedGroupKey(), m_groupMore->mapToGlobal(QPoint(0, m_groupMore->height())));
  });
  connect(m_viewCombo, &QComboBox::activated, this, [this](int p_row) {
    selectView(m_viewProxy->mapToSource(m_viewProxy->index(p_row, 0)).row());
  });
  connect(m_lists, &QListView::clicked, this, [this](const QModelIndex &p_index) {
    if (p_index.data(TodoListModel::GroupRole).toBool()) {
      focusGroup(p_index.data(TodoListModel::GroupIdRole).toString());
    } else {
      selectView(p_index.row());
    }
  });
  connect(m_lists, &QListView::doubleClicked, this, [this](const QModelIndex &p_index) {
    if (p_index.data(TodoListModel::GroupRole).toBool()) {
      toggleGroup(p_index.data(TodoListModel::GroupIdRole).toString());
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
  connect(m_tasks, &QListView::clicked, this, &TodoEditor::openTask);
  connect(m_tasks, &QListView::activated, this, &TodoEditor::openTask);
  connect(m_tasks, &QWidget::customContextMenuRequested, this, [this](const QPoint &p_position) {
    auto index = m_tasks->indexAt(p_position);
    if (!index.isValid()) {
      index = m_tasks->currentIndex();
    }
    showTaskMenu(index.data(TodoTaskModel::IdRole).toString(),
                 m_tasks->viewport()->mapToGlobal(p_position));
  });
  connect(m_sort, &QComboBox::activated, this, [this](int p_index) {
    if (m_controller) {
      endTyping();
      m_controller->setSortOrder(static_cast<TodoSortOrder>(p_index));
    }
  });
  connect(m_tagFilter, &QPushButton::clicked, this, [this]() {
    if (m_controller) {
      endTyping();
      m_controller->setTagFilter({});
    }
  });
  connect(m_listMore, &QPushButton::clicked, this, [this]() {
    if (m_controller) {
      showListMenu(m_controller->getProjection().m_listKey,
                   m_listMore->mapToGlobal(QPoint(0, m_listMore->height())));
    }
  });
  connect(m_listTitle, &QLineEdit::textEdited, this, [this](const QString &p_text) {
    if (!m_updating && m_controller) {
      const auto key = m_controller->getProjection().m_listKey;
      command([this, key, p_text]() { return m_controller->renameList(key, p_text); }, m_listTitle);
    }
  });
  connect(m_description, &SimpleMarkdownEditor::textChanged, this, [this]() {
    if (!m_updating && m_controller) {
      const auto key = m_controller->getProjection().m_listKey;
      const auto text = m_description->toPlainText();
      command([this, key, text]() { return m_controller->setListDescription(key, text); },
              m_description->getTextEdit());
    }
  });
  connect(m_destination, &QComboBox::activated, this,
          [this]() { m_lastDestination = m_destination->currentData().toString(); });
  connect(m_addHasDue, &QCheckBox::toggled, this, [this]() { refreshQuickAdd(); });
  connect(m_addTask, &QLineEdit::textChanged, this, [this]() { refreshQuickAdd(); });
  connect(m_addTask, &QLineEdit::returnPressed, this, &TodoEditor::addTask);
  connect(m_addButton, &QPushButton::clicked, this, &TodoEditor::addTask);
}

void TodoEditor::createDetails() {
  m_detailsScroll = named(new QScrollArea(m_splitter), "todoDetailsScroll", tr("Task details"));
  m_detailsScroll->setWidgetResizable(true);
  m_detailsScroll->setFrameShape(QFrame::NoFrame);
  m_details = named(new QWidget(m_detailsScroll), "todoDetails", tr("Task details"));
  m_detailsScroll->setWidget(m_details);
  auto *layout = new QVBoxLayout(m_details);
  spacing(layout);
  m_back = named(new QPushButton(tr("Back to tasks"), m_details), "todoBack",
                 tr("Close details and return to tasks"));
  layout->addWidget(m_back);
  m_title = named(new QLineEdit(m_details), "todoTitle", tr("Task title"));
  m_title->setPlaceholderText(tr("Untitled task"));
  layout->addWidget(m_title);
  auto *state = new QHBoxLayout;
  state->setSpacing(4);
  m_checked = named(new QCheckBox(tr("Completed"), m_details), "todoChecked", tr("Task completed"));
  m_important =
      named(new QCheckBox(tr("Important"), m_details), "todoImportant", tr("Task important"));
  state->addWidget(m_checked);
  state->addWidget(m_important);
  layout->addLayout(state);
  m_myDay =
      named(new QCheckBox(tr("My Day"), m_details), "todoMyDay", tr("Include task in My Day"));
  layout->addWidget(m_myDay);
  m_myDayDate = named(new QLabel(m_details), "todoMyDayDate", tr("Stored My Day date"));
  m_myDayDate->setWordWrap(true);
  layout->addWidget(m_myDayDate);
  m_hasDue = named(new QCheckBox(tr("Due"), m_details), "todoHasDue", tr("Task has a due date"));
  layout->addWidget(m_hasDue);
  auto *due = new QHBoxLayout;
  due->setSpacing(4);
  m_due = named(new QDateEdit(QDate::currentDate(), m_details), "todoDue", tr("Task due date"));
  m_due->setCalendarPopup(true);
  m_due->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
  m_clearDue =
      named(new QPushButton(tr("Clear"), m_details), "todoClearDue", tr("Clear task due date"));
  due->addWidget(m_due, 1);
  due->addWidget(m_clearDue);
  layout->addLayout(due);
  auto *stepLabel = named(new QLabel(tr("Steps"), m_details), "todoStepsLabel", tr("Steps"));
  m_steps = named(new QListWidget(m_details), "todoSteps", tr("Task steps"));
  m_steps->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_steps->setMinimumHeight(80);
  m_steps->setMaximumHeight(160);
  stepLabel->setBuddy(m_steps);
  layout->addWidget(stepLabel);
  layout->addWidget(m_steps);
  auto *step = new QHBoxLayout;
  step->setSpacing(4);
  m_stepTitle = named(new QLineEdit(m_details), "todoStepTitle", tr("Selected step title"));
  m_stepTitle->setPlaceholderText(tr("Untitled step"));
  m_stepChecked =
      named(new QCheckBox(tr("Done"), m_details), "todoStepChecked", tr("Selected step completed"));
  step->addWidget(m_stepTitle, 1);
  step->addWidget(m_stepChecked);
  layout->addLayout(step);
  auto *stepActions = new QHBoxLayout;
  stepActions->setSpacing(4);
  m_stepUp = named(new QPushButton(tr("Up"), m_details), "todoStepUp", tr("Move step up"));
  m_stepDown = named(new QPushButton(tr("Down"), m_details), "todoStepDown", tr("Move step down"));
  m_stepDelete =
      named(new QPushButton(tr("Delete"), m_details), "todoStepDelete", tr("Delete step"));
  stepActions->addWidget(m_stepUp);
  stepActions->addWidget(m_stepDown);
  stepActions->addWidget(m_stepDelete);
  layout->addLayout(stepActions);
  m_addStep = named(new QLineEdit(m_details), "todoAddStep", tr("Add a step; press Enter"));
  m_addStep->setPlaceholderText(tr("Add a step"));
  layout->addWidget(m_addStep);

  auto *tagsLabel = named(new QLabel(tr("Tags"), m_details), "todoTagsLabel", tr("Tags"));
  auto *tagScroll = named(new QScrollArea(m_details), "todoTagsScroll", tr("Task tags"));
  tagScroll->setWidgetResizable(true);
  tagScroll->setFrameShape(QFrame::NoFrame);
  tagScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  tagScroll->setMinimumHeight(48);
  tagScroll->setMaximumHeight(64);
  m_tagChips = named(new QWidget(tagScroll), "todoTagChips", tr("Click a tag to filter tasks"));
  m_tagLayout = new QHBoxLayout(m_tagChips);
  m_tagLayout->setContentsMargins(0, 0, 0, 0);
  m_tagLayout->setSpacing(4);
  tagScroll->setWidget(m_tagChips);
  layout->addWidget(tagsLabel);
  layout->addWidget(tagScroll);
  m_tagInput = named(new QLineEdit(m_details), "todoTagInput",
                     tr("New tag; Enter commits, spaces and commas are literal"));
  m_tagInput->setPlaceholderText(tr("Add a tag"));
  tagsLabel->setBuddy(m_tagInput);
  layout->addWidget(m_tagInput);
  auto *noteLabel =
      named(new QLabel(tr("Markdown note"), m_details), "todoNoteLabel", tr("Markdown note"));
  m_note = named(new SimpleMarkdownEditor(m_services, m_details), "todoNote",
                 tr("Task note in Markdown"));
  named(m_note->getTextEdit(), "todoNoteTextEdit", tr("Task note in Markdown"));
  m_note->getTextEdit()->setUndoRedoEnabled(false);
  m_note->setMinimumHeight(136);
  noteLabel->setBuddy(m_note->getTextEdit());
  layout->addWidget(noteLabel);
  layout->addWidget(m_note, 1);
  auto *actions = new QHBoxLayout;
  actions->setSpacing(4);
  m_move = named(new QPushButton(tr("Move to list"), m_details), "todoMoveTask",
                 tr("Move task to another list"));
  m_delete =
      named(new QPushButton(tr("Delete task"), m_details), "todoDeleteTask", tr("Delete task"));
  actions->addWidget(m_move);
  actions->addWidget(m_delete);
  layout->addLayout(actions);

  connect(m_back, &QPushButton::clicked, this, &TodoEditor::closeDetails);
  connect(m_title, &QLineEdit::textEdited, this, [this](const QString &p_text) {
    updateTask([&](TodoTaskFields &p_fields) { p_fields.m_title = p_text; }, m_title);
  });
  connect(m_checked, &QCheckBox::toggled, this, [this](bool p_checked) {
    updateTask([&](TodoTaskFields &p_fields) { p_fields.m_checked = p_checked; });
  });
  connect(m_important, &QCheckBox::toggled, this, [this](bool p_checked) {
    updateTask([&](TodoTaskFields &p_fields) { p_fields.m_important = p_checked; });
  });
  connect(m_myDay, &QCheckBox::toggled, this, [this](bool p_checked) {
    updateTask([&](TodoTaskFields &p_fields) {
      p_fields.m_myDay = p_checked ? m_controller->getProjection().m_today : QDate();
    });
  });
  connect(m_hasDue, &QCheckBox::toggled, this, [this](bool p_checked) {
    updateTask(
        [&](TodoTaskFields &p_fields) { p_fields.m_due = p_checked ? m_due->date() : QDate(); });
  });
  connect(m_due, &QDateEdit::dateChanged, this, [this](const QDate &p_date) {
    if (m_hasDue->isChecked()) {
      updateTask([&](TodoTaskFields &p_fields) { p_fields.m_due = p_date; });
    }
  });
  connect(m_clearDue, &QPushButton::clicked, this,
          [this]() { updateTask([](TodoTaskFields &p_fields) { p_fields.m_due = QDate(); }); });
  connect(m_note, &SimpleMarkdownEditor::textChanged, this, [this]() {
    if (!m_updating) {
      const auto text = m_note->toPlainText();
      updateTask([&](TodoTaskFields &p_fields) { p_fields.m_note = text; }, m_note->getTextEdit());
    }
  });
  connect(m_steps, &QListWidget::currentRowChanged, this, [this](int p_row) {
    if (!m_updating) {
      endTyping();
      m_stepIndex = p_row;
      QScopedValueRollback<bool> updating(m_updating, true);
      refreshSteps(selectedItem());
    }
  });
  connect(m_steps, &QListWidget::itemChanged, this, [this](QListWidgetItem *p_item) {
    if (m_updating || !canMutate()) {
      return;
    }
    const int row = m_steps->row(p_item);
    const auto *item = selectedItem();
    if (!item || row < 0 || row >= item->m_steps.size()) {
      return;
    }
    const auto key = item->m_key;
    const auto title = item->m_steps[row].m_title;
    const bool checked = p_item->checkState() == Qt::Checked;
    command([this, key, row, title, checked]() {
      return m_controller->updateStep(key, row, title, checked);
    });
  });
  connect(m_stepTitle, &QLineEdit::textEdited, this, [this]() { updateStep(true); });
  connect(m_stepChecked, &QCheckBox::toggled, this, [this]() { updateStep(false); });
  connect(m_stepUp, &QPushButton::clicked, this, [this]() { moveStep(-1); });
  connect(m_stepDown, &QPushButton::clicked, this, [this]() { moveStep(1); });
  connect(m_stepDelete, &QPushButton::clicked, this, [this]() {
    const auto *item = selectedItem();
    if (!item || m_stepIndex < 0) {
      return;
    }
    const auto key = item->m_key;
    const int row = m_stepIndex;
    command([this, key, row]() { return m_controller->removeStep(key, row); });
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
  connect(m_tagInput, &QLineEdit::returnPressed, this, [this]() { editTags(m_tagInput->text()); });
  connect(m_move, &QPushButton::clicked, this, [this]() { moveTaskToList(m_detailsKey); });
  connect(m_delete, &QPushButton::clicked, this, [this]() { deleteTask(m_detailsKey); });
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
  m_viewProxy->setSourceModel(p_controller ? p_controller->getListModel() : nullptr);
  m_tasks->setModel(p_controller ? p_controller->getTaskModel() : nullptr);
  if (p_controller) {
    connect(p_controller, &TodoViewWindowController::projectionChanged, this, &TodoEditor::refresh);
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
  resetForBuffer();
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
         m_controller->getDocument().isValid();
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
  m_groupCombo->clear();
  m_groupMore->setEnabled(false);
  m_listTitle->setReadOnly(!writable);
  m_description->setReadOnly(!writable);
  if (!m_controller) {
    m_listTitle->hide();
    m_description->hide();
    m_listMore->hide();
    m_viewTitle->setText(tr("All Tasks"));
    m_tagFilter->hide();
    refreshDetails();
    refreshQuickAdd();
    return;
  }
  const auto &projection = m_controller->getProjection();
  const int row = m_controller->getListModel()->rowForView(projection.m_view, projection.m_listKey);
  auto *listModel = m_controller->getListModel();
  m_viewCombo->setCurrentIndex(m_viewProxy->mapFromSource(listModel->index(row, 0)).row());
  for (int i = 0; i < listModel->rowCount(); ++i) {
    const auto index = listModel->index(i, 0);
    m_lists->setRowHidden(i, listModel->listAt(i) &&
                                 !index.data(TodoListModel::ExpandedRole).toBool());
  }
  const int groupRow = listModel->rowForGroup(projection.m_groupKey);
  const int selectedRow =
      (m_groupFocused || (row >= 0 && m_lists->isRowHidden(row))) && groupRow >= 0 ? groupRow : row;
  m_lists->setCurrentIndex(listModel->index(selectedRow, 0));
  for (const auto &group : m_controller->getDocument().m_groups) {
    m_groupCombo->addItem(groupName(group), group.m_key);
  }
  m_groupCombo->setCurrentIndex(m_groupCombo->findData(projection.m_groupKey));
  m_groupCombo->setEnabled(m_groupCombo->count() > 0);
  m_groupMore->setEnabled(groupRow >= 0);
  if (m_search->text() != projection.m_search) {
    m_search->setText(projection.m_search);
  }
  m_sort->setCurrentIndex(static_cast<int>(projection.m_sort));
  m_tagFilter->setText(tr("Tag: %1 (clear)").arg(projection.m_tag));
  m_tagFilter->setVisible(!projection.m_tag.isEmpty());
  const bool filtered = !projection.m_search.isEmpty() || !projection.m_tag.isEmpty();
  const auto *list = projection.m_view == TodoViewKind::List && !filtered
                         ? m_controller->getDocument().findList(projection.m_listKey)
                         : nullptr;
  m_listTitle->setVisible(list != nullptr);
  m_description->setVisible(list != nullptr);
  m_listMore->setVisible(list != nullptr);
  m_listMore->setEnabled(writable);
  m_viewTitle->setVisible(!list);
  if (list) {
    if (!preservesField(m_listTitle) && m_listTitle->text() != list->m_title) {
      m_listTitle->setText(list->m_title);
    }
    if (!preservesField(m_description->getTextEdit()) &&
        m_description->toPlainText() != list->m_description) {
      m_description->setPlainText(list->m_description);
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
  if (key != m_detailsKey && !(m_committing && m_typingField)) {
    endTyping();
    m_stepIndex = -1;
    m_tagInput->clear();
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
  m_hasDue->setEnabled(writable);
  m_due->setEnabled(writable && fields.m_due.isValid());
  m_clearDue->setEnabled(writable && fields.m_due.isValid());
  m_move->setEnabled(writable);
  m_delete->setEnabled(writable);
  m_addStep->setEnabled(writable);
  m_tagInput->setEnabled(writable);
  if (!preservesField(m_title) && m_title->text() != fields.m_title) {
    m_title->setText(fields.m_title);
  }
  m_checked->setChecked(fields.m_checked);
  m_important->setChecked(fields.m_important);
  m_myDay->setChecked(m_controller && fields.m_myDay.isValid() &&
                      fields.m_myDay == m_controller->getProjection().m_today);
  m_myDayDate->setText(
      fields.m_myDay.isValid()
          ? tr("My Day: %1").arg(QLocale().toString(fields.m_myDay, QLocale::ShortFormat))
          : QString());
  m_myDayDate->setAccessibleName(m_myDayDate->text());
  m_myDayDate->setVisible(fields.m_myDay.isValid());
  m_hasDue->setChecked(fields.m_due.isValid());
  m_due->setDate(fields.m_due.isValid() ? fields.m_due : QDate::currentDate());
  if (!preservesField(m_note->getTextEdit()) && m_note->toPlainText() != fields.m_note) {
    m_note->setPlainText(fields.m_note);
  }
  refreshSteps(item);
  refreshTags(item);
  updateLayout();
}

void TodoEditor::refreshSteps(const TodoItem *p_item) {
  const int count = p_item ? int(p_item->m_steps.size()) : 0;
  const bool writable = canMutate() && p_item;
  // Reuse rows so a typing echo never destroys the focused step editor or scroll position.
  while (m_steps->count() > count) {
    delete m_steps->takeItem(m_steps->count() - 1);
  }
  for (int i = 0; i < count; ++i) {
    const auto &step = p_item->m_steps[i];
    auto *row = i < m_steps->count() ? m_steps->item(i) : new QListWidgetItem(m_steps);
    const auto title = step.m_title.trimmed().isEmpty() ? tr("Untitled step") : step.m_title;
    row->setText(title);
    row->setData(Qt::AccessibleTextRole,
                 tr("%1, %2").arg(title, step.m_checked ? tr("completed") : tr("not completed")));
    row->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled |
                  (writable ? Qt::ItemIsUserCheckable : Qt::NoItemFlags));
    row->setCheckState(step.m_checked ? Qt::Checked : Qt::Unchecked);
  }
  m_stepIndex = count ? qBound(0, m_stepIndex < 0 ? 0 : m_stepIndex, count - 1) : -1;
  m_steps->setCurrentRow(m_stepIndex);
  const auto *step = m_stepIndex >= 0 ? &p_item->m_steps[m_stepIndex] : nullptr;
  m_stepTitle->setEnabled(step != nullptr);
  m_stepTitle->setReadOnly(!writable);
  const QString title = step ? step->m_title : QString();
  if (!preservesField(m_stepTitle) && m_stepTitle->text() != title) {
    m_stepTitle->setText(title);
  }
  m_stepChecked->setEnabled(writable && step);
  m_stepChecked->setChecked(step && step->m_checked);
  m_stepUp->setEnabled(writable && m_stepIndex > 0);
  m_stepDown->setEnabled(writable && m_stepIndex >= 0 && m_stepIndex + 1 < count);
  m_stepDelete->setEnabled(writable && step);
}

void TodoEditor::refreshTags(const TodoItem *p_item) {
  const QStringList tags = p_item ? p_item->m_fields.m_tags : QStringList();
  if (tags != m_renderedTags) {
    m_renderedTags = tags;
    while (auto *entry = m_tagLayout->takeAt(0)) {
      if (entry->widget()) {
        entry->widget()->hide();
        for (auto *child : entry->widget()->findChildren<QObject *>()) {
          child->setObjectName({});
        }
        entry->widget()->deleteLater();
      }
      delete entry;
    }
    for (int i = 0; i < tags.size(); ++i) {
      auto *chip = new QWidget(m_tagChips);
      auto *layout = new QHBoxLayout(chip);
      layout->setContentsMargins(0, 0, 0, 0);
      layout->setSpacing(0);
      auto *tag = new QToolButton(chip);
      tag->setObjectName(QStringLiteral("todoTagChip_%1").arg(i));
      tag->setText(tags[i]);
      tag->setAccessibleName(tr("Filter by tag %1").arg(tags[i]));
      auto *remove = new QToolButton(chip);
      remove->setObjectName(QStringLiteral("todoTagRemove_%1").arg(i));
      remove->setText(tr("Remove"));
      remove->setAccessibleName(tr("Remove tag %1").arg(tags[i]));
      remove->setProperty("todoTagRemove", true);
      layout->addWidget(tag);
      layout->addWidget(remove);
      m_tagLayout->addWidget(chip);
      const auto value = tags[i];
      connect(tag, &QToolButton::clicked, this, [this, value]() {
        if (m_controller) {
          endTyping();
          m_controller->setTagFilter(value);
        }
      });
      connect(remove, &QToolButton::clicked, this, [this, i]() { editTags({}, i); });
    }
    m_tagLayout->addStretch();
  }
  for (auto *button : m_tagChips->findChildren<QToolButton *>()) {
    if (button->property("todoTagRemove").toBool()) {
      button->setEnabled(canMutate() && p_item);
    }
  }
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

void TodoEditor::updateLayout() {
  const bool narrow = width() < 840;
  if (narrow != m_narrow) {
    m_narrow = narrow;
    if (narrow) {
      m_navigation->layout()->addWidget(m_searchArea);
      m_navigation->layout()->addWidget(m_creationArea);
      m_pages->addWidget(m_detailsScroll);
      m_sidebar->hide();
      m_navigation->show();
    } else {
      static_cast<QVBoxLayout *>(m_sidebar->layout())->insertWidget(0, m_searchArea);
      m_sidebar->layout()->addWidget(m_creationArea);
      m_pages->removeWidget(m_detailsScroll);
      m_splitter->addWidget(m_detailsScroll);
      m_sidebar->show();
      m_navigation->hide();
      m_pages->setCurrentWidget(m_taskPane);
      m_splitter->setSizes({260, qMax(1, width() - 596), 320});
    }
  }
  m_groupCombo->setVisible(narrow);
  m_groupMore->setVisible(narrow);
  m_back->setText(narrow ? tr("Back to tasks") : tr("Close details"));
  if (narrow) {
    m_pages->setCurrentWidget(m_detailsOpen ? static_cast<QWidget *>(m_detailsScroll) : m_taskPane);
  } else {
    m_detailsScroll->setVisible(m_detailsOpen);
  }
}

void TodoEditor::resizeEvent(QResizeEvent *p_event) {
  QWidget::resizeEvent(p_event);
  updateLayout();
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
  focusTasks();
}

void TodoEditor::focusSearch() {
  if (m_narrow && m_detailsOpen) {
    closeDetails();
  }
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
  if (m_narrow) {
    m_groupCombo->setFocus(Qt::OtherFocusReason);
  } else {
    m_lists->setFocus(Qt::OtherFocusReason);
    m_lists->scrollTo(m_lists->currentIndex());
  }
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

void TodoEditor::focusTasks() {
  m_groupFocused = false;
  if (m_controller) {
    auto *model = m_controller->getListModel();
    const auto &projection = m_controller->getProjection();
    const int row = model->rowForView(projection.m_view, projection.m_listKey);
    if (row >= 0 && !m_lists->isRowHidden(row)) {
      m_lists->setCurrentIndex(model->index(row, 0));
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
    fields.m_myDay = projection.m_today;
  }
  if (!filtered && projection.m_view == TodoViewKind::Important) {
    fields.m_important = true;
  }
  if (!filtered && projection.m_view == TodoViewKind::Planned && m_addHasDue->isChecked()) {
    fields.m_due = m_addDue->date();
  }
  const QPointer<TodoEditor> self(this);
  if (command([this, listKey, fields]() { return m_controller->addTask(listKey, fields); }) &&
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

void TodoEditor::updateStep(bool p_typing) {
  if (m_updating || !canMutate()) {
    return;
  }
  const auto *item = selectedItem();
  if (!item || m_stepIndex < 0 || m_stepIndex >= item->m_steps.size()) {
    return;
  }
  const auto key = item->m_key;
  const int index = m_stepIndex;
  const auto title = m_stepTitle->text();
  const bool checked = m_stepChecked->isChecked();
  command([this, key, index, title,
           checked]() { return m_controller->updateStep(key, index, title, checked); },
          p_typing ? m_stepTitle : nullptr);
}

void TodoEditor::moveStep(int p_direction) {
  const auto *item = selectedItem();
  const int from = m_stepIndex;
  const int to = from + p_direction;
  if (!item || from < 0 || to < 0 || to >= item->m_steps.size()) {
    return;
  }
  const auto key = item->m_key;
  m_stepIndex = to;
  if (!command([this, key, from, to]() { return m_controller->moveStep(key, from, to); })) {
    m_stepIndex = from;
    refresh();
  }
}

void TodoEditor::editTags(const QString &p_add, int p_remove) {
  const auto *item = selectedItem();
  const auto addition = p_add.trimmed();
  if (!item || !canMutate() || (p_remove < 0 && addition.isEmpty())) {
    return;
  }
  QStringList tags;
  for (int i = 0; i < item->m_fields.m_tags.size(); ++i) {
    if (i != p_remove) {
      const auto &tag = item->m_fields.m_tags[i];
      if (!tags.contains(tag, Qt::CaseInsensitive)) {
        tags.append(tag);
      }
    }
  }
  if (!addition.isEmpty() && !tags.contains(addition, Qt::CaseInsensitive)) {
    tags.append(addition);
  }
  const auto key = item->m_key;
  auto fields = item->m_fields;
  fields.m_tags = tags;
  const QPointer<TodoEditor> self(this);
  if (command([this, key, fields]() { return m_controller->updateTask(key, fields); }) && self &&
      p_remove < 0) {
    m_tagInput->clear();
  }
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

void TodoEditor::deleteGroup(const QString &p_key) {
  if (!canMutate()) {
    return;
  }
  const auto *group = m_controller->getDocument().findGroup(p_key);
  if (!group) {
    return;
  }
  endTyping();
  const auto token = gesture();
  const QPointer<TodoEditor> self(this);
  if (group->m_listCount > 0) {
    const auto answer = QMessageBox::question(
        this, tr("Delete group"),
        tr("Delete group \"%1\" and all its lists and tasks?").arg(groupName(*group)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (!self || answer != QMessageBox::Yes || !accepts(token)) {
      return;
    }
  }
  command([this, p_key]() { return m_controller->removeGroup(p_key); });
}

void TodoEditor::moveGroup(const QString &p_key, int p_direction) {
  if (!canMutate()) {
    return;
  }
  const auto &groups = m_controller->getDocument().m_groups;
  int position = -1;
  for (int i = 0; i < groups.size(); ++i) {
    if (groups[i].m_key == p_key) {
      position = i;
      break;
    }
  }
  const int target = position + p_direction;
  if (position < 0 || target < 0 || target >= groups.size()) {
    return;
  }
  const int before = p_direction < 0 ? target : target + 1;
  const QString beforeKey = before < groups.size() ? groups[before].m_key : QString();
  command([this, p_key, beforeKey]() { return m_controller->moveGroup(p_key, beforeKey); });
}

void TodoEditor::showGroupMenu(const QString &p_key, const QPoint &p_globalPosition) {
  if (!m_controller || !m_controller->getDocument().findGroup(p_key)) {
    return;
  }
  endTyping();
  m_groupFocused = true;
  m_controller->setSelectedGroupKey(p_key);
  const auto token = gesture();
  auto *menu = named(new QMenu(this), "todoGroupMenu", tr("Group actions"));
  menu->setAttribute(Qt::WA_DeleteOnClose);
  auto add = [&](const QString &p_label, const char *p_name, const std::function<void()> &p_action,
                 bool p_mutation = true) {
    auto *action = menu->addAction(p_label);
    action->setObjectName(QString::fromLatin1(p_name));
    action->setEnabled(!p_mutation || canMutate());
    connect(action, &QAction::triggered, this, [this, token, p_action, p_mutation]() {
      if (p_mutation ? accepts(token) : acceptsNavigation(token)) {
        p_action();
      }
    });
  };
  add(tr("New List"), "todoGroupNewListAction", [this, p_key]() { addList(p_key); });
  add(tr("Rename"), "todoRenameGroupAction", [this, p_key]() { renameGroup(p_key); });
  add(tr("Move Up"), "todoGroupUpAction", [this, p_key]() { moveGroup(p_key, -1); });
  add(tr("Move Down"), "todoGroupDownAction", [this, p_key]() { moveGroup(p_key, 1); });
  const bool collapsed = m_controller->getProjection().m_collapsedGroups.contains(p_key);
  add(
      collapsed ? tr("Expand") : tr("Collapse"), "todoToggleGroupAction",
      [this, p_key]() { toggleGroup(p_key); }, false);
  menu->addSeparator();
  add(tr("Delete"), "todoDeleteGroupAction", [this, p_key]() { deleteGroup(p_key); });
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

void TodoEditor::moveTaskToList(const QString &p_key) {
  if (!canMutate() || !m_controller->getDocument().findItem(p_key)) {
    return;
  }
  endTyping();
  const auto token = gesture();
  const QPointer<TodoEditor> self(this);
  QPointer<QDialog> dialog = named(new QDialog(this), "todoMoveDialog", tr("Move task to list"));
  dialog->setWindowTitle(tr("Move task to list"));
  auto *layout = new QVBoxLayout(dialog);
  spacing(layout);
  auto *destination = named(new QComboBox(dialog), "todoMoveDestination", tr("Destination list"));
  for (const auto &list : m_controller->getDocument().m_lists) {
    destination->addItem(qualifiedListName(m_controller->getDocument(), list), list.m_key);
  }
  const auto *item = m_controller->getDocument().findItem(p_key);
  destination->setCurrentIndex(destination->findData(item->m_listKey));
  layout->addWidget(destination);
  auto *buttons =
      named(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog),
            "todoMoveButtons", tr("Move task actions"));
  named(buttons->button(QDialogButtonBox::Ok), "todoMoveAccept", tr("Move task"));
  named(buttons->button(QDialogButtonBox::Cancel), "todoMoveCancel", tr("Cancel move"));
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  const int result = dialog->exec();
  if (!dialog) {
    return;
  }
  const auto key = destination->currentData().toString();
  delete dialog;
  if (self && result == QDialog::Accepted && accepts(token)) {
    command([this, p_key, key]() { return m_controller->moveTask(p_key, key, {}); });
  }
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

void TodoEditor::moveListToGroup(const QString &p_key) {
  if (!canMutate()) {
    return;
  }
  const auto *list = m_controller->getDocument().findList(p_key);
  const auto *group = list ? m_controller->getDocument().groupForList(*list) : nullptr;
  if (!group) {
    return;
  }
  endTyping();
  m_groupFocused = false;
  m_controller->setView(TodoViewKind::List, p_key);
  const auto token = gesture();
  const QPointer<TodoEditor> self(this);
  QPointer<QDialog> dialog =
      named(new QDialog(this), "todoMoveListDialog", tr("Move list to group"));
  dialog->setWindowTitle(tr("Move list to group"));
  auto *layout = new QVBoxLayout(dialog);
  spacing(layout);
  auto *destination = named(new QComboBox(dialog), "todoMoveListGroup", tr("Destination group"));
  addGroupChoices(destination, m_controller->getDocument());
  destination->setCurrentIndex(group->m_isDefault ? 0 : destination->findData(group->m_key));
  layout->addWidget(destination);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
  named(buttons->button(QDialogButtonBox::Ok), "todoMoveListAccept", tr("Move list"));
  named(buttons->button(QDialogButtonBox::Cancel), "todoMoveListCancel", tr("Cancel move"));
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  const int result = dialog->exec();
  if (!dialog) {
    return;
  }
  const auto groupKey = destination->currentData().toString();
  delete dialog;
  if (self && result == QDialog::Accepted && accepts(token)) {
    const QPointer<TodoViewWindowController> controller(m_controller);
    if (command([controller, p_key, groupKey]() {
          return controller->moveList(p_key, groupKey, {});
        }) &&
        self && m_controller == controller) {
      const auto &projection = controller->getProjection();
      const auto *moved = controller->getDocument().findList(projection.m_listKey);
      const auto *owner = moved ? controller->getDocument().groupForList(*moved) : nullptr;
      if (owner) {
        controller->setGroupExpanded(owner->m_key, true);
      }
    }
  }
}

void TodoEditor::showListMenu(const QString &p_key, const QPoint &p_globalPosition) {
  if (!m_controller || !m_controller->getDocument().findList(p_key)) {
    return;
  }
  endTyping();
  const auto token = gesture();
  auto *menu = new QMenu(this);
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
  add(tr("Rename"), "todoRenameListAction", [this, p_key]() { renameList(p_key); });
  add(tr("Edit description"), "todoEditDescriptionAction", [this, p_key]() {
    endTyping();
    m_controller->setView(TodoViewKind::List, p_key);
    m_controller->setSearch({});
    m_controller->setTagFilter({});
    if (m_narrow) {
      closeDetails();
    }
    m_description->getTextEdit()->setFocus(Qt::OtherFocusReason);
  });
  add(tr("Move to group"), "todoMoveListGroupAction", [this, p_key]() { moveListToGroup(p_key); });
  add(tr("Move Up"), "todoListUpAction", [this, p_key]() { moveList(p_key, -1); });
  add(tr("Move Down"), "todoListDownAction", [this, p_key]() { moveList(p_key, 1); });
  menu->addSeparator();
  add(tr("Delete"), "todoDeleteListAction", [this, p_key]() { deleteList(p_key); });
  menu->popup(p_globalPosition);
}

void TodoEditor::showTaskMenu(const QString &p_key, const QPoint &p_globalPosition) {
  if (!m_controller || !m_controller->getDocument().findItem(p_key)) {
    return;
  }
  endTyping();
  m_controller->setSelectedItemKey(p_key);
  const auto token = gesture();
  auto *menu = new QMenu(this);
  menu->setAttribute(Qt::WA_DeleteOnClose);
  auto add = [&](const QString &p_label, const char *p_name, const std::function<void()> &p_action,
                 bool p_enabled) {
    auto *action = menu->addAction(p_label);
    action->setObjectName(QString::fromLatin1(p_name));
    action->setEnabled(canMutate() && p_enabled);
    connect(action, &QAction::triggered, this, [this, token, p_action]() {
      if (accepts(token)) {
        p_action();
      }
    });
  };
  add(
      tr("Toggle completion"), "todoToggleCompletedAction",
      [this]() {
        updateTask([](TodoTaskFields &p_fields) { p_fields.m_checked = !p_fields.m_checked; });
      },
      true);
  add(
      tr("Toggle importance"), "todoToggleImportantAction",
      [this]() {
        updateTask([](TodoTaskFields &p_fields) { p_fields.m_important = !p_fields.m_important; });
      },
      true);
  add(tr("Move to list"), "todoMoveTaskAction", [this, p_key]() { moveTaskToList(p_key); }, true);
  const bool reorder = m_controller->getTaskModel()->isReorderingEnabled();
  add(tr("Move Up"), "todoTaskUpAction", [this, p_key]() { moveTask(p_key, -1); }, reorder);
  add(tr("Move Down"), "todoTaskDownAction", [this, p_key]() { moveTask(p_key, 1); }, reorder);
  menu->addSeparator();
  add(tr("Delete"), "todoDeleteTaskAction", [this, p_key]() { deleteTask(p_key); }, true);
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
  const bool projected = line == m_title || line == m_listTitle || line == m_stepTitle ||
                         text == m_note->getTextEdit() || text == m_description->getTextEdit() ||
                         m_due->isAncestorOf(p_widget);
  auto *menu = new QMenu(this);
  menu->setAttribute(Qt::WA_DeleteOnClose);
  auto *undo = menu->addAction(tr("Undo"));
  undo->setObjectName(QStringLiteral("todoNativeUndoAction"));
  undo->setEnabled(canMutate());
  connect(undo, &QAction::triggered, this, [this]() {
    if (canMutate()) {
      emit undoRequested();
    }
  });
  auto *redo = menu->addAction(tr("Redo"));
  redo->setObjectName(QStringLiteral("todoNativeRedoAction"));
  redo->setEnabled(canMutate());
  connect(redo, &QAction::triggered, this, [this]() {
    if (canMutate()) {
      emit redoRequested();
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
  if (!widget || (widget != this && !isAncestorOf(widget)) || widget->window() != window()) {
    return QWidget::eventFilter(p_object, p_event);
  }
  if (p_event->type() == QEvent::FocusOut && widget == m_typingField) {
    endTyping();
  }
  if (p_event->type() == QEvent::Hide && widget == this) {
    endTyping();
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
        if (undo) {
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
    if (key->key() == Qt::Key_Delete) {
      deleteTask(id);
      return true;
    }
    if (key->modifiers() == Qt::AltModifier &&
        (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down)) {
      moveTask(id, key->key() == Qt::Key_Up ? -1 : 1);
      return true;
    }
  }
  if (m_controller && (widget == m_lists || widget == m_groupCombo || widget == m_groupMore)) {
    const bool groupRow =
        widget != m_lists || m_lists->currentIndex().data(TodoListModel::GroupRole).toBool();
    const auto groupKey = widget == m_lists
                              ? m_lists->currentIndex().data(TodoListModel::GroupIdRole).toString()
                              : selectedGroupKey();
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
    if (widget == m_lists && groupRow && !groupKey.isEmpty() &&
        key->modifiers() == Qt::NoModifier) {
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
  if (widget == m_steps && key->modifiers() == Qt::AltModifier &&
      (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down)) {
    moveStep(key->key() == Qt::Key_Up ? -1 : 1);
    return true;
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

void TodoEditor::zoom(int p_delta) {
  const int next = qBound(-4, m_zoomDelta + p_delta, 12);
  const int change = next - m_zoomDelta;
  if (!change) {
    return;
  }
  m_zoomDelta = next;
  auto nextFont = font();
  if (nextFont.pointSizeF() > 0) {
    nextFont.setPointSizeF(qMax(1.0, nextFont.pointSizeF() + change));
  } else {
    nextFont.setPixelSize(qMax(1, nextFont.pixelSize() + change));
  }
  setFont(nextFont);
  m_note->zoom(change);
  m_description->zoom(change);
  m_tasks->doItemsLayout();
  m_lists->doItemsLayout();
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
  ++m_generation;
}

void TodoEditor::resetForBuffer() {
  invalidateGestures();
  m_detailsOpen = false;
  m_groupFocused = false;
  m_detailsKey.clear();
  m_lastDestination.clear();
  m_stepIndex = -1;
  {
    QScopedValueRollback<bool> updating(m_updating, true);
    m_addTask->clear();
    m_addStep->clear();
    m_tagInput->clear();
    m_addHasDue->setChecked(false);
    m_addDue->setDate(QDate::currentDate());
  }
  refresh();
}
