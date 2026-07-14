#include "historysticker.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QLocale>
#include <QShowEvent>
#include <QToolButton>
#include <QVBoxLayout>

#include <core/fileopensettings.h>
#include <core/hookcontext.h>
#include <core/hooknames.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/historyservice.h>
#include <core/services/hookmanager.h>
#include <models/historylistmodel.h>
#include <views/filenodedelegate.h>

using namespace vnotex;

HistorySticker::HistorySticker(ServiceLocator &p_services, QWidget *p_parent)
    : Sticker(p_services, p_parent) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  m_model = new HistoryListModel(m_services.get<HistoryService>(), this);

  // Filter bar shown only in day-filter mode; lets the user return to recent.
  m_filterBar = new QWidget(this);
  m_filterBar->setObjectName(QStringLiteral("historyFilterBar"));
  auto *filterLayout = new QHBoxLayout(m_filterBar);
  filterLayout->setContentsMargins(4, 2, 4, 2);
  m_filterLabel = new QLabel(m_filterBar);
  filterLayout->addWidget(m_filterLabel);
  filterLayout->addStretch();
  m_clearButton = new QToolButton(m_filterBar);
  m_clearButton->setObjectName(QStringLiteral("historyClearFilter"));
  m_clearButton->setText(QStringLiteral("\u2715"));
  m_clearButton->setAutoRaise(true);
  m_clearButton->setToolTip(tr("Show recent files"));
  filterLayout->addWidget(m_clearButton);
  m_filterBar->setVisible(false);
  layout->addWidget(m_filterBar);

  connect(m_clearButton, &QToolButton::clicked, this, &HistorySticker::clearDateFilter);

  m_listView = new QListView(this);
  m_listView->setModel(m_model);
  m_delegate = new FileNodeDelegate(m_services, m_listView);
  m_listView->setItemDelegate(m_delegate);
  m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
  m_listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_listView->setDragEnabled(false);
  m_listView->setAcceptDrops(false);
  layout->addWidget(m_listView);

  connect(m_listView, &QListView::activated, this, &HistorySticker::onItemActivated);

  // Subscribe to the calendar-date-changed hook. The calendar hook has no typed
  // event struct, so use the raw (HookContext&, const QVariantMap&) callback.
  m_hookManager = m_services.get<HookManager>();
  if (m_hookManager) {
    m_dateHookId = m_hookManager->addAction(
        HookNames::DashboardCalendarDateChanged,
        [this](HookContext &, const QVariantMap &p_args) { onCalendarDateChanged(p_args); });
  }
}

HistorySticker::~HistorySticker() {
  // Prevent a dangling callback firing into a destroyed sticker.
  if (m_hookManager && m_dateHookId >= 0) {
    m_hookManager->removeAction(m_dateHookId);
  }
}

QString HistorySticker::typeId() const { return QStringLiteral("history"); }

QString HistorySticker::titleText() const { return tr("History"); }

void HistorySticker::showEvent(QShowEvent *p_event) {
  reloadCurrentMode();
  Sticker::showEvent(p_event);
}

void HistorySticker::onCalendarDateChanged(const QVariantMap &p_args) {
  const QString s = p_args.value(QStringLiteral("date")).toString();
  const QDate d = QDate::fromString(s, QStringLiteral("yyyy-MM-dd"));
  if (!d.isValid()) {
    // Missing / invalid payload: preserve the current mode.
    return;
  }
  m_activeDate = d;
  reloadCurrentMode();
}

void HistorySticker::reloadCurrentMode() {
  if (!m_model) {
    return;
  }
  if (m_activeDate.isValid()) {
    m_model->loadForDate(m_activeDate, kHistoryLimit);
  } else {
    m_model->loadRecent(kHistoryLimit);
  }
  updateFilterBar();
}

void HistorySticker::updateFilterBar() {
  if (!m_filterBar) {
    return;
  }
  if (m_activeDate.isValid()) {
    if (m_filterLabel) {
      m_filterLabel->setText(
          tr("Opened on %1").arg(QLocale().toString(m_activeDate, QLocale::ShortFormat)));
    }
    m_filterBar->setVisible(true);
  } else {
    m_filterBar->setVisible(false);
  }
}

void HistorySticker::clearDateFilter() {
  m_activeDate = QDate();
  reloadCurrentMode();
}

void HistorySticker::onItemActivated(const QModelIndex &p_index) {
  if (!p_index.isValid() || !m_model) {
    return;
  }
  const NodeIdentifier nodeId = m_model->nodeIdFromIndex(p_index);
  if (!nodeId.isValid()) {
    return;
  }
  auto *bufferSvc = m_services.get<BufferService>();
  if (bufferSvc) {
    bufferSvc->openBuffer(nodeId, FileOpenSettings{});
  }
}
