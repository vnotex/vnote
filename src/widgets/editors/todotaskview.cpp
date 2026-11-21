#include "todotaskview.h"

#include <QApplication>
#include <QCursor>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScrollBar>
#include <QStyleOptionViewItem>

#include <cmath>

#include <core/servicelocator.h>
#include <gui/services/themeservice.h>
#include <gui/utils/treechildcountdelegate.h>
#include <models/todomodels.h>

using namespace vnotex;

namespace {
class TaskDelegate : public TreeChildCountDelegate {
public:
  TaskDelegate(ServiceLocator &p_services, QObject *p_parent)
      : TreeChildCountDelegate(p_parent), m_theme(p_services.get<ThemeService>()) {
    Q_ASSERT(m_theme);
  }

  void setShowLocation(bool p_show) {
    if (m_showLocation == p_show) {
      return;
    }
    m_showLocation = p_show;
    if (auto *view = qobject_cast<QListView *>(parent())) {
      view->viewport()->update();
    }
  }

  static QRect rowRect(const QStyleOptionViewItem &p_option) {
    return p_option.rect.adjusted(0, 2, 0, -2);
  }

  static QRect checkRect(const QStyleOptionViewItem &p_option) {
    const int side = qMax(20, p_option.fontMetrics.height());
    const auto row = rowRect(p_option);
    return QRect(row.left() + 16, row.center().y() - side / 2, side, side);
  }
  static QRect starRect(const QStyleOptionViewItem &p_option) {
    const int side = qMax(20, p_option.fontMetrics.height());
    const auto row = rowRect(p_option);
    return QRect(row.right() - side - 16, row.center().y() - side / 2, side, side);
  }

  static QRect myDayRect(const QStyleOptionViewItem &p_option) {
    const auto star = starRect(p_option);
    return star.translated(-star.width() - 12, 0);
  }

  void paint(QPainter *p_painter, const QStyleOptionViewItem &p_option,
             const QModelIndex &p_index) const override {
    QStyleOptionViewItem option(p_option);
    initStyleOption(&option, p_index);
    const auto *widget = option.widget;
    const auto *style = widget ? widget->style() : QApplication::style();
    option.text.clear();
    option.features &= ~QStyleOptionViewItem::HasCheckIndicator;
    const bool section = p_index.data(TodoTaskModel::SectionRole).toBool();
    if (section) {
      style->drawControl(QStyle::CE_ItemViewItem, &option, p_painter, widget);
    }
    p_painter->save();
    p_painter->setClipRect(option.rect);
    p_painter->setRenderHint(QPainter::Antialiasing);
    const auto group =
        (option.state & QStyle::State_Enabled) ? QPalette::Active : QPalette::Disabled;
    const bool selected = option.state & QStyle::State_Selected;
    // Match the theme's list-item color pairs, not the OS HighlightedText color.
    const auto foreground =
        selected ? (!section || (option.state & QStyle::State_Active)
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
    if (section) {
      const int side = qMax(20, option.fontMetrics.height());
      const QRect circle(option.rect.left() + 8, option.rect.center().y() - side / 2, side, side);
      const auto visual = [&](const QRect &p_rect) {
        return QStyle::visualRect(option.direction, option.rect, p_rect);
      };
      auto badge = childCountBadge(option, p_index.data(TodoTaskModel::CompletedCountRole).toInt());
      auto titleRect = option.rect.adjusted(circle.width() + 20, 0, -8, 0);
      if (!badge.m_rect.isEmpty()) {
        titleRect.setRight(qMin(titleRect.right(), badge.m_rect.left() - badge.m_gap - 1));
        badge.m_rect = visual(badge.m_rect);
      }
      QStyleOption arrow;
      arrow.rect = visual(circle.adjusted(4, 4, -4, -4));
      arrow.palette = option.palette;
      arrow.state = option.state;
      style->drawPrimitive(p_index.data(TodoTaskModel::ExpandedRole).toBool()
                               ? QStyle::PE_IndicatorArrowDown
                           : option.direction == Qt::RightToLeft ? QStyle::PE_IndicatorArrowLeft
                                                                 : QStyle::PE_IndicatorArrowRight,
                           &arrow, p_painter, widget);
      p_painter->drawText(
          visual(titleRect),
          QStyle::visualAlignment(option.direction, Qt::AlignVCenter | Qt::AlignLeading),
          option.fontMetrics.elidedText(p_index.data().toString(), Qt::ElideRight,
                                        qMax(0, titleRect.width())));
      option.palette.setColor(
          QPalette::Text, QColor(m_theme->paletteColor(QStringLiteral("widgets#qlistview#fg"))));
      option.palette.setColor(QPalette::Disabled, QPalette::Text,
                              QColor(m_theme->paletteColor(QStringLiteral("base#disabled#fg"))));
      paintChildCount(p_painter, badge, option);
      p_painter->restore();
      return;
    }

    const auto row = rowRect(option);
    if (selected || (option.state & QStyle::State_MouseOver)) {
      auto background = textColor;
      if (selected) {
        // Keep the active pair like Lists; Native's inactive alternate-base can be transparent.
        background = QColor(
            m_theme->paletteColor(QStringLiteral("widgets#qlistview#item#selected#active#bg")));
      } else {
        background.setAlpha(18);
      }
      p_painter->setPen(Qt::NoPen);
      p_painter->setBrush(background);
      p_painter->drawRoundedRect(row, 4, 4);
    }
    if (option.state & QStyle::State_HasFocus) {
      p_painter->setPen(QPen(textColor, 1, Qt::DotLine));
      p_painter->setBrush(Qt::NoBrush);
      p_painter->drawRoundedRect(row.adjusted(1, 1, -1, -1), 4, 4);
    }
    const QRect circle = checkRect(option);
    const bool checked = p_index.data(TodoTaskModel::CheckedRole).toBool();
    paintTodoCompletionIndicator(p_painter, circle, checked, textColor);
    const QRect sun = myDayRect(option);
    paintTodoMyDayIndicator(p_painter, sun, p_index.data(TodoTaskModel::InMyDayRole).toBool(),
                            textColor);
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

    auto titleFont = option.font;
    titleFont.setStrikeOut(checked);
    QStringList summaries;
    if (m_showLocation) {
      const auto groupTitle = p_index.data(TodoTaskModel::SourceGroupNameRole).toString();
      const auto list = p_index.data(TodoTaskModel::SourceListNameRole).toString();
      summaries.append(groupTitle.isEmpty() ? list
                                            : TodoTaskView::tr("%1 / %2").arg(groupTitle, list));
    }
    const auto due = p_index.data(TodoTaskModel::DueRole).toDate();
    if (due.isValid()) {
      summaries.append(
          TodoTaskView::tr("Due %1").arg(QLocale().toString(due, QLocale::ShortFormat)));
    }
    const auto myDay = p_index.data(TodoTaskModel::MyDayRole).toDate();
    if (myDay.isValid()) {
      summaries.append(
          TodoTaskView::tr("My Day: %1").arg(QLocale().toString(myDay, QLocale::ShortFormat)));
    }
    const int steps = p_index.data(TodoTaskModel::StepsTotalRole).toInt();
    if (steps) {
      summaries.append(TodoTaskView::tr("%1/%2 steps")
                           .arg(p_index.data(TodoTaskModel::StepsCompletedRole).toInt())
                           .arg(steps));
    }
    if (!p_index.data(TodoTaskModel::NoteRole).toString().isEmpty()) {
      summaries.append(TodoTaskView::tr("Note"));
    }
    auto secondaryFont = option.font;
    if (secondaryFont.pointSizeF() > 2) {
      secondaryFont.setPointSizeF(secondaryFont.pointSizeF() - 1);
    }
    const QFontMetrics secondaryMetrics(secondaryFont);
    const int titleHeight = option.fontMetrics.height();
    const int textHeight = titleHeight + (summaries.isEmpty() ? 0 : secondaryMetrics.height() + 4);
    const QRect textRect(circle.right() + 14, row.center().y() - textHeight / 2,
                         qMax(0, sun.left() - circle.right() - 28), textHeight);
    p_painter->setFont(titleFont);
    p_painter->setPen(textColor);
    p_painter->drawText(textRect, Qt::AlignLeft | Qt::AlignTop,
                        QFontMetrics(titleFont).elidedText(p_index.data().toString(),
                                                           Qt::ElideRight, textRect.width()));
    if (!summaries.isEmpty()) {
      p_painter->setFont(secondaryFont);
      p_painter->setPen(selected ? textColor
                                 : QColor(m_theme->paletteColor(QStringLiteral("base#muted#fg"))));
      p_painter->drawText(textRect, Qt::AlignLeft | Qt::AlignBottom,
                          secondaryMetrics.elidedText(summaries.join(QStringLiteral(" / ")),
                                                      Qt::ElideRight, textRect.width()));
    }
    p_painter->restore();
  }

  QSize sizeHint(const QStyleOptionViewItem &p_option, const QModelIndex &p_index) const override {
    const int height = p_option.fontMetrics.height();
    return QSize(0, p_index.data(TodoTaskModel::SectionRole).toBool() ? qMax(44, height + 24)
                                                                      : qMax(56, height * 2 + 24));
  }

  bool editorEvent(QEvent *, QAbstractItemModel *, const QStyleOptionViewItem &,
                   const QModelIndex &) override {
    // The view owns control gestures; Qt's square-checkbox editor must not mutate the model.
    return false;
  }

private:
  ThemeService *m_theme;
  bool m_showLocation = true;
};
} // namespace

void vnotex::paintTodoCompletionIndicator(QPainter *p_painter, const QRect &p_rect, bool p_checked,
                                          const QColor &p_foreground) {
  p_painter->setPen(QPen(p_foreground, 1.5));
  p_painter->setBrush(Qt::NoBrush);
  p_painter->drawEllipse(p_rect.adjusted(2, 2, -2, -2));
  if (p_checked) {
    p_painter->setPen(QPen(p_foreground, 2));
    const QPoint middle(p_rect.left() + p_rect.width() * 4 / 10,
                        p_rect.top() + p_rect.height() * 7 / 10);
    p_painter->drawLine(QPoint(p_rect.left() + p_rect.width() / 4, p_rect.center().y()), middle);
    p_painter->drawLine(
        middle, QPoint(p_rect.right() - p_rect.width() / 4, p_rect.top() + p_rect.height() / 3));
  }
}

void vnotex::paintTodoMyDayIndicator(QPainter *p_painter, const QRect &p_rect, bool p_checked,
                                     const QColor &p_foreground) {
  p_painter->setPen(QPen(p_foreground, 1.5, Qt::SolidLine, Qt::RoundCap));
  p_painter->setBrush(p_checked ? QBrush(p_foreground) : Qt::NoBrush);
  const QPointF center(p_rect.center());
  const qreal side = qMin(p_rect.width(), p_rect.height());
  const qreal radius = side * 0.20;
  p_painter->drawEllipse(center, radius, radius);
  constexpr qreal diagonal = 0.70710678118654752440;
  static constexpr QPointF directions[] = {
      {1, 0}, {0, 1}, {diagonal, diagonal}, {diagonal, -diagonal}};
  for (const auto &direction : directions) {
    const auto inner = direction * (side * 0.34);
    const auto outer = direction * (side * 0.46);
    p_painter->drawLine(center + inner, center + outer);
    p_painter->drawLine(center - inner, center - outer);
  }
}

TodoTaskView::TodoTaskView(ServiceLocator &p_services, QWidget *p_parent) : QListView(p_parent) {
  setItemDelegate(new TaskDelegate(p_services, this));
  setEditTriggers(QAbstractItemView::NoEditTriggers);
  setSelectionMode(QAbstractItemView::SingleSelection);
  setMouseTracking(true);
  setFrameShape(QFrame::NoFrame);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &TodoTaskView::updateTaskCursor);
  connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, &TodoTaskView::updateTaskCursor);
  auto *theme = p_services.get<ThemeService>();
  Q_ASSERT(theme);
  connect(theme, &ThemeService::themeChanged, this, [this]() { viewport()->update(); });
  connect(this, &QListView::clicked, this, [this](const QModelIndex &p_index) {
    if (m_bodyRelease && isTaskIndex(p_index)) {
      emit taskClicked(p_index);
    }
  });
  connect(this, &QListView::activated, this, [this](const QModelIndex &p_index) {
    if (!m_suppressActivation && isTaskIndex(p_index)) {
      emit taskActivated(p_index);
    }
  });
  connect(this, &QListView::doubleClicked, this, [this](const QModelIndex &p_index) {
    // Single-click styles omit native double-click activation. Bridge only Qt-validated clicks.
    if (m_bodyDoubleClick && !m_suppressActivation && isTaskIndex(p_index) &&
        style()->styleHint(QStyle::SH_ItemView_ActivateItemOnSingleClick, nullptr, this)) {
      emit taskActivated(p_index);
    }
  });
}

void TodoTaskView::setShowLocation(bool p_show) {
  static_cast<TaskDelegate *>(itemDelegate())->setShowLocation(p_show);
}

void TodoTaskView::setModel(QAbstractItemModel *p_model) {
  if (model() == p_model) {
    return;
  }
  for (auto &connection : m_modelConnections) {
    disconnect(connection);
    connection = {};
  }
  cancelControlCommit();
  const QPointer<TodoTaskView> self(this);
  const QPointer<QAbstractItemModel> guardedModel(p_model);
  QListView::setModel(p_model);
  if (!self || model() != guardedModel.data()) {
    return;
  }
  if (guardedModel) {
    m_modelConnections = {
        connect(p_model, &QAbstractItemModel::modelAboutToBeReset, this,
                &TodoTaskView::cancelControlCommit),
        connect(p_model, &QAbstractItemModel::modelReset, this, &TodoTaskView::updateTaskCursor),
        connect(p_model, &QAbstractItemModel::rowsInserted, this, &TodoTaskView::updateTaskCursor),
        connect(p_model, &QAbstractItemModel::rowsRemoved, this, &TodoTaskView::updateTaskCursor),
        connect(p_model, &QAbstractItemModel::rowsMoved, this, &TodoTaskView::updateTaskCursor),
        connect(p_model, &QAbstractItemModel::layoutChanged, this, &TodoTaskView::updateTaskCursor),
        connect(p_model, &QAbstractItemModel::dataChanged, this, &TodoTaskView::updateTaskCursor),
        connect(p_model, &QObject::destroyed, this, [this]() {
          cancelControlCommit();
          setTaskCursor({});
        })};
  }
  updateTaskCursor();
}

TodoTaskView::HitPart TodoTaskView::hitPart(const QModelIndex &p_index,
                                            const QPoint &p_position) const {
  if (!p_index.isValid() || p_index.model() != model()) {
    return HitPart::None;
  }
  if (p_index.data(TodoTaskModel::SectionRole).toBool()) {
    return HitPart::Section;
  }
  QStyleOptionViewItem option;
  initViewItemOption(&option);
  option.rect = visualRect(p_index);
  if (TaskDelegate::checkRect(option).contains(p_position)) {
    return HitPart::Check;
  }
  if (TaskDelegate::myDayRect(option).contains(p_position)) {
    return HitPart::MyDay;
  }
  if (TaskDelegate::starRect(option).contains(p_position)) {
    return HitPart::Star;
  }
  return HitPart::Body;
}

bool TodoTaskView::isTaskIndex(const QModelIndex &p_index) const {
  return p_index.isValid() && p_index.model() == model() && (p_index.flags() & Qt::ItemIsEnabled) &&
         !p_index.data(TodoTaskModel::SectionRole).toBool() &&
         !p_index.data(TodoTaskModel::IdRole).toString().isEmpty();
}

void TodoTaskView::toggleControl(const QModelIndex &p_index, HitPart p_part) {
  if (!isEnabled() || !p_index.isValid() || p_index.model() != model() ||
      !(p_index.flags() & Qt::ItemIsEnabled)) {
    return;
  }
  const bool section = p_index.data(TodoTaskModel::SectionRole).toBool();
  int role = 0;
  if (p_part == HitPart::Section && section) {
    role = TodoTaskModel::ExpandedRole;
  } else if (!section && (p_index.flags() & Qt::ItemIsUserCheckable)) {
    if (p_part == HitPart::Check) {
      role = TodoTaskModel::CheckedRole;
    } else if (p_part == HitPart::MyDay) {
      role = TodoTaskModel::InMyDayRole;
    } else if (p_part == HitPart::Star) {
      role = TodoTaskModel::ImportantRole;
    }
  }
  if (role) {
    const bool value = !p_index.data(role).toBool();
    // Controllers may synchronously reset the model or destroy this view from setData().
    model()->setData(p_index, value, role);
  }
}

void TodoTaskView::cancelControlCommit() {
  m_pressedIndex = QModelIndex();
  m_pressedPart = HitPart::None;
  // Keep consuming a captured gesture through release, even after its row disappears.
}

void TodoTaskView::mousePressEvent(QMouseEvent *p_event) {
  cancelControlCommit();
  m_controlGesture = false;
  const auto index = indexAt(p_event->position().toPoint());
  const auto part = hitPart(index, p_event->position().toPoint());
  m_lastPressWasControl =
      p_event->button() == Qt::LeftButton && part != HitPart::None && part != HitPart::Body;
  if (!m_lastPressWasControl) {
    QListView::mousePressEvent(p_event);
    return;
  }
  m_controlGesture = true;
  m_pressedIndex = index;
  m_pressedPart = part;
  p_event->accept();
  const QPointer<TodoTaskView> self(this);
  const QPersistentModelIndex pressed(index);
  setFocus(Qt::MouseFocusReason);
  if (!self || !pressed.isValid() || pressed.model() != model()) {
    return;
  }
  // Controls still select their row, including the read-only Completed header.
  setCurrentIndex(pressed);
}

void TodoTaskView::mouseMoveEvent(QMouseEvent *p_event) {
  setTaskCursor(indexAt(p_event->position().toPoint()));
  if (m_controlGesture) {
    // No native press was delivered for this control; never drag Qt's previous pressed row.
    p_event->accept();
    return;
  }
  QListView::mouseMoveEvent(p_event);
}

void TodoTaskView::mouseReleaseEvent(QMouseEvent *p_event) {
  if (p_event->button() == Qt::LeftButton && m_controlGesture) {
    const QPersistentModelIndex pressed(m_pressedIndex);
    const auto part = m_pressedPart;
    m_controlGesture = false;
    cancelControlCommit();
    p_event->accept();
    const auto position = p_event->position().toPoint();
    if (pressed.isValid() && pressed.model() == model() && indexAt(position) == pressed &&
        hitPart(pressed, position) == part) {
      toggleControl(pressed, part);
    }
    return;
  }
  const QPointer<TodoTaskView> self(this);
  const bool previousRelease = m_bodyRelease;
  const bool previousSuppression = m_suppressActivation;
  m_bodyRelease = p_event->button() == Qt::LeftButton &&
                  hitPart(indexAt(p_event->position().toPoint()), p_event->position().toPoint()) ==
                      HitPart::Body;
  // Qt styles may emit activated from a single release; that is not semantic activation.
  m_suppressActivation = true;
  QListView::mouseReleaseEvent(p_event);
  if (self) {
    m_bodyRelease = previousRelease;
    m_suppressActivation = previousSuppression;
  }
}

void TodoTaskView::mouseDoubleClickEvent(QMouseEvent *p_event) {
  const auto part = hitPart(indexAt(p_event->position().toPoint()), p_event->position().toPoint());
  if (p_event->button() == Qt::LeftButton &&
      (m_lastPressWasControl || (part != HitPart::None && part != HitPart::Body))) {
    // The first release already committed. Swallow the double-click and trailing release.
    cancelControlCommit();
    m_controlGesture = true;
    m_lastPressWasControl = true;
    p_event->accept();
    return;
  }
  const QPointer<TodoTaskView> self(this);
  const bool previousDoubleClick = m_bodyDoubleClick;
  const bool previousSuppression = m_suppressActivation;
  m_bodyDoubleClick = p_event->button() == Qt::LeftButton && part == HitPart::Body;
  m_suppressActivation = previousSuppression || !m_bodyDoubleClick;
  QListView::mouseDoubleClickEvent(p_event);
  if (self) {
    m_bodyDoubleClick = previousDoubleClick;
    m_suppressActivation = previousSuppression;
  }
}

void TodoTaskView::keyPressEvent(QKeyEvent *p_event) {
  const auto key = p_event->key();
  if (key != Qt::Key_Space && key != Qt::Key_Return && key != Qt::Key_Enter) {
    QListView::keyPressEvent(p_event);
    return;
  }
  p_event->accept();
  if (p_event->isAutoRepeat()) {
    return;
  }
  const auto index = currentIndex();
  if (index.data(TodoTaskModel::SectionRole).toBool()) {
    toggleControl(index, HitPart::Section);
  } else if (key == Qt::Key_Space) {
    toggleControl(index, HitPart::Check);
  } else if (isTaskIndex(index)) {
    emit taskActivated(index);
  }
}

bool TodoTaskView::viewportEvent(QEvent *p_event) {
  const QPointer<TodoTaskView> self(this);
  const bool handled = QListView::viewportEvent(p_event);
  if (self) {
    if (p_event->type() == QEvent::Enter) {
      setTaskCursor(indexAt(static_cast<QEnterEvent *>(p_event)->position().toPoint()));
    } else if (p_event->type() == QEvent::Resize) {
      updateTaskCursor();
    } else if (p_event->type() == QEvent::Leave) {
      setTaskCursor({});
    }
  }
  return handled;
}

void TodoTaskView::changeEvent(QEvent *p_event) {
  const QPointer<TodoTaskView> self(this);
  QListView::changeEvent(p_event);
  if (!self) {
    return;
  }
  if (p_event->type() == QEvent::EnabledChange) {
    if (!isEnabled()) {
      cancelControlCommit();
    }
    updateTaskCursor();
  } else if (p_event->type() == QEvent::FontChange || p_event->type() == QEvent::StyleChange ||
             p_event->type() == QEvent::PaletteChange) {
    updateTaskCursor();
    viewport()->update();
  }
}

void TodoTaskView::updateTaskCursor() {
  const auto *port = viewport();
  setTaskCursor(port->underMouse() ? indexAt(port->mapFromGlobal(QCursor::pos())) : QModelIndex());
}

void TodoTaskView::setTaskCursor(const QModelIndex &p_index) {
  auto *port = viewport();
  const bool completed = isEnabled() && p_index.data(TodoTaskModel::SectionRole).toBool();
  if (completed) {
    if (port->cursor().shape() != Qt::PointingHandCursor) {
      port->setCursor(Qt::PointingHandCursor);
    }
  } else if (port->testAttribute(Qt::WA_SetCursor)) {
    port->unsetCursor();
  }
}
