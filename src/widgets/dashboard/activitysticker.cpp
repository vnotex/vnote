#include "activitysticker.h"

#include <QHBoxLayout>
#include <QFont>
#include <QHash>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QShowEvent>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include <core/hookcontext.h>
#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/hookmanager.h>

using namespace vnotex;

namespace {
QString formatActiveTime(qint64 p_ms) {
  const qint64 totalMinutes = p_ms / 60000;
  const qint64 hours = totalMinutes / 60;
  const qint64 minutes = totalMinutes % 60;
  if (hours > 0) {
    return ActivitySticker::tr("%1h %2m").arg(hours).arg(minutes);
  }
  return ActivitySticker::tr("%1m").arg(minutes);
}
} // namespace

namespace vnotex {

// A colorful rounded-rectangle badge showing a metric value over a caption.
// Self-painted so it stays theme-neutral regardless of the active QSS palette.
class MetricBadge : public QWidget {
public:
  MetricBadge(const QString &p_caption, const QColor &p_color, QWidget *p_parent = nullptr)
      : QWidget(p_parent), m_caption(p_caption), m_color(p_color) {
    // Tall enough for the value and caption to sit on two comfortable rows.
    setMinimumSize(56, 56);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }

  void setValue(const QString &p_value) {
    if (m_value != p_value) {
      m_value = p_value;
      update();
    }
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF area = QRectF(rect()).adjusted(2, 2, -2, -2);
    const qreal radius = qMin<qreal>(10.0, area.height() / 3.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_color);
    painter.drawRoundedRect(area, radius, radius);

    // White text reads well over the saturated badge colors.
    painter.setPen(QColor(Qt::white));

    QFont valueFont = font();
    valueFont.setBold(true);
    valueFont.setPointSizeF(valueFont.pointSizeF() + 1.0);
    painter.setFont(valueFont);
    const QRectF valueRect(area.left(), area.top() + area.height() * 0.12, area.width(),
                           area.height() * 0.52);
    painter.drawText(valueRect, Qt::AlignCenter,
                     valueFont.pointSizeF() > 0
                         ? painter.fontMetrics().elidedText(m_value, Qt::ElideRight,
                                                            static_cast<int>(valueRect.width()))
                         : m_value);

    QFont captionFont = font();
    captionFont.setPointSizeF(qMax(6.0, captionFont.pointSizeF() - 1.5));
    painter.setFont(captionFont);
    QColor captionColor(Qt::white);
    captionColor.setAlpha(210);
    painter.setPen(captionColor);
    const QRectF captionRect(area.left(), area.top() + area.height() * 0.58, area.width(),
                             area.height() * 0.36);
    painter.drawText(captionRect, Qt::AlignCenter, m_caption);
  }

private:
  QString m_caption;
  QString m_value;
  QColor m_color;
};

// Minimal custom bar chart: one normalized bar per day of active time. No
// external charting dependency; theme-neutral via palette roles.
class ActivityChartWidget : public QWidget {
public:
  explicit ActivityChartWidget(QWidget *p_parent = nullptr) : QWidget(p_parent) {
    setMinimumHeight(56);
    // Track motion without a pressed button so bars report a hover tooltip.
    setMouseTracking(true);
  }

  void setData(const QVector<ActivityStatsService::ActivityDay> &p_days) {
    m_days = p_days;
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    const QRect area = chartArea();
    if (m_days.isEmpty() || area.width() <= 0 || area.height() <= 0) {
      return;
    }

    const qint64 maxMs = maxActiveMs();
    if (maxMs <= 0) {
      return;
    }

    const int count = m_days.size();
    const qreal slotWidth = static_cast<qreal>(area.width()) / count;
    const qreal barWidth = qMax<qreal>(1.0, slotWidth * 0.7);
    const QColor barColor = palette().color(QPalette::Highlight);

    painter.setPen(Qt::NoPen);
    painter.setBrush(barColor);
    for (int i = 0; i < count; ++i) {
      const qreal ratio = static_cast<qreal>(m_days.at(i).activeMs) / maxMs;
      const int barHeight = qMax(0, static_cast<int>(ratio * area.height()));
      const int x = area.left() + static_cast<int>(i * slotWidth + (slotWidth - barWidth) / 2.0);
      const int y = area.bottom() - barHeight;
      painter.drawRect(QRectF(x, y, barWidth, barHeight));
    }
  }

  void mouseMoveEvent(QMouseEvent *p_event) override {
    const int idx = slotAt(p_event->pos());
    if (idx < 0) {
      QToolTip::hideText();
      QWidget::mouseMoveEvent(p_event);
      return;
    }
    const ActivityStatsService::ActivityDay &day = m_days.at(idx);
    const QString dateText = QLocale().toString(day.date, QLocale::ShortFormat);
    const QString when = day.date == QDate::currentDate()
                             ? ActivitySticker::tr("Today (%1)").arg(dateText)
                             : dateText;
    const QString detail = day.activeMs > 0
                               ? ActivitySticker::tr("%1 active").arg(formatActiveTime(day.activeMs))
                               : ActivitySticker::tr("No activity");
    QToolTip::showText(p_event->globalPos(), QStringLiteral("%1\u2003%2").arg(when, detail), this);
    QWidget::mouseMoveEvent(p_event);
  }

private:
  QRect chartArea() const { return rect().adjusted(1, 1, -1, -1); }

  qint64 maxActiveMs() const {
    qint64 maxMs = 0;
    for (const auto &day : m_days) {
      maxMs = qMax(maxMs, day.activeMs);
    }
    return maxMs;
  }

  // Slot (day) index under a widget-local point, or -1 when outside the chart.
  int slotAt(const QPoint &p_pos) const {
    const QRect area = chartArea();
    const int count = m_days.size();
    if (count <= 0 || area.width() <= 0 || !area.contains(p_pos)) {
      return -1;
    }
    const qreal slotWidth = static_cast<qreal>(area.width()) / count;
    const int idx = static_cast<int>((p_pos.x() - area.left()) / slotWidth);
    return qBound(0, idx, count - 1);
  }

  QVector<ActivityStatsService::ActivityDay> m_days;
};

} // namespace vnotex

ActivitySticker::ActivitySticker(ServiceLocator &p_services, QWidget *p_parent)
    : Sticker(p_services, p_parent) {
  m_statsService = m_services.get<ActivityStatsService>();

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  // Filter bar (shown only when a Calendar day is selected).
  m_filterBar = new QWidget(this);
  m_filterBar->setObjectName(QStringLiteral("activityFilterBar"));
  auto *filterLayout = new QHBoxLayout(m_filterBar);
  filterLayout->setContentsMargins(4, 2, 4, 2);
  m_filterLabel = new QLabel(m_filterBar);
  filterLayout->addWidget(m_filterLabel);
  filterLayout->addStretch();
  m_clearButton = new QToolButton(m_filterBar);
  m_clearButton->setObjectName(QStringLiteral("activityClearFilter"));
  m_clearButton->setText(QStringLiteral("\u2715"));
  m_clearButton->setAutoRaise(true);
  m_clearButton->setToolTip(tr("Back to today"));
  filterLayout->addWidget(m_clearButton);
  m_filterBar->setVisible(false);
  layout->addWidget(m_filterBar);
  connect(m_clearButton, &QToolButton::clicked, this, &ActivitySticker::clearDateFilter);

  // Summary metric badges: colorful rounded-rect chips in a row.
  auto *summary = new QWidget(this);
  auto *badgeRow = new QHBoxLayout(summary);
  badgeRow->setContentsMargins(4, 2, 4, 2);
  badgeRow->setSpacing(4);
  auto addBadge = [&](const QString &p_caption, const QColor &p_color) -> MetricBadge * {
    auto *badge = new MetricBadge(p_caption, p_color, summary);
    badgeRow->addWidget(badge);
    return badge;
  };
  m_activeTimeValue = addBadge(tr("Active"), QColor(0x2f, 0x80, 0xed)); // blue
  m_createdValue = addBadge(tr("Created"), QColor(0x27, 0xae, 0x60));   // green
  m_readValue = addBadge(tr("Read"), QColor(0xf2, 0x99, 0x4a));        // amber
  m_editedValue = addBadge(tr("Edited"), QColor(0x9b, 0x51, 0xe0));    // purple
  layout->addWidget(summary);

  // 30-day trailing active-time chart.
  m_chart = new ActivityChartWidget(this);
  layout->addWidget(m_chart, 1);

  // Subscribe to the calendar-date-changed hook (raw callback: no typed event).
  m_hookManager = m_services.get<HookManager>();
  if (m_hookManager) {
    m_dateHookId = m_hookManager->addAction(
        HookNames::DashboardCalendarDateChanged,
        [this](HookContext &, const QVariantMap &p_args) { onCalendarDateChanged(p_args); });
  }
}

ActivitySticker::~ActivitySticker() {
  if (m_hookManager && m_dateHookId >= 0) {
    m_hookManager->removeAction(m_dateHookId);
  }
}

QString ActivitySticker::typeId() const { return QStringLiteral("activity"); }

QString ActivitySticker::titleText() const { return tr("Activity"); }

void ActivitySticker::showEvent(QShowEvent *p_event) {
  reloadCurrentMode();
  Sticker::showEvent(p_event);
}

void ActivitySticker::onCalendarDateChanged(const QVariantMap &p_args) {
  const QString s = p_args.value(QStringLiteral("date")).toString();
  const QDate d = QDate::fromString(s, QStringLiteral("yyyy-MM-dd"));
  if (!d.isValid()) {
    // Missing / invalid payload: preserve the current mode.
    return;
  }
  m_activeDate = d;
  reloadCurrentMode();
}

void ActivitySticker::reloadCurrentMode() {
  if (!m_statsService) {
    return;
  }
  const QDate day = m_activeDate.isValid() ? m_activeDate : QDate::currentDate();

  // Summary for the active day.
  const ActivityStatsService::ActivityRange summary = m_statsService->getRange(day, day);
  if (m_activeTimeValue) {
    m_activeTimeValue->setValue(formatActiveTime(summary.activeMs));
  }
  if (m_createdValue) {
    m_createdValue->setValue(QString::number(summary.notesCreated));
  }
  if (m_readValue) {
    m_readValue->setValue(QString::number(summary.notesRead));
  }
  if (m_editedValue) {
    m_editedValue->setValue(QString::number(summary.notesEdited));
  }

  // 30-day trailing chart ending at the active day. vxcore returns a SPARSE
  // series (only days with activity), so normalize it into one ordered slot
  // per calendar day in [day-29, day], zero-filling gaps.
  const QDate chartStart = day.addDays(-(kChartDays - 1));
  const ActivityStatsService::ActivityRange chartRange =
      m_statsService->getRange(chartStart, day);
  QHash<QDate, ActivityStatsService::ActivityDay> byDate;
  byDate.reserve(chartRange.daily.size());
  for (const auto &d : chartRange.daily) {
    byDate.insert(d.date, d);
  }
  QVector<ActivityStatsService::ActivityDay> series;
  series.reserve(kChartDays);
  for (int i = 0; i < kChartDays; ++i) {
    const QDate slotDate = chartStart.addDays(i);
    auto it = byDate.constFind(slotDate);
    if (it != byDate.constEnd()) {
      series.append(it.value());
    } else {
      ActivityStatsService::ActivityDay empty;
      empty.date = slotDate;
      series.append(empty);
    }
  }
  if (m_chart) {
    m_chart->setData(series);
  }

  updateFilterBar();
}

void ActivitySticker::updateFilterBar() {
  if (!m_filterBar) {
    return;
  }
  if (m_activeDate.isValid()) {
    if (m_filterLabel) {
      m_filterLabel->setText(
          tr("Activity on %1").arg(QLocale().toString(m_activeDate, QLocale::ShortFormat)));
    }
    m_filterBar->setVisible(true);
  } else {
    m_filterBar->setVisible(false);
  }
}

void ActivitySticker::clearDateFilter() {
  m_activeDate = QDate();
  reloadCurrentMode();
}
