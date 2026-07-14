#include "historyunitedentry.h"

#include <QLabel>
#include <QListView>
#include <QSortFilterProxyModel>

#include "entrywidgetfactory.h"
#include <core/fileopensettings.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/historyservice.h>
#include <gui/services/themeservice.h>
#include <models/historylistmodel.h>
#include <models/inodelistmodel.h>
#include <views/filenodedelegate.h>

using namespace vnotex;

HistoryUnitedEntry::HistoryUnitedEntry(ServiceLocator &p_services, UnitedEntryMgr *p_mgr,
                                       QObject *p_parent)
    : IUnitedEntry("history", tr("Recently opened files"), p_mgr, p_parent),
      m_services(p_services),
      m_helper(p_services.get<ThemeService>()) {
  auto *themeService = m_services.get<ThemeService>();
  connect(themeService, &ThemeService::themeChanged, this, [this]() { m_helper.refreshIcons(); });
}

void HistoryUnitedEntry::initOnFirstProcess() {
  m_model = new HistoryListModel(m_services.get<HistoryService>(), this);

  m_proxyModel = new QSortFilterProxyModel(this);
  m_proxyModel->setSourceModel(m_model);
  m_proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
  m_proxyModel->setFilterKeyColumn(0);

  auto *listView = new QListView();
  listView->setModel(m_proxyModel);
  m_delegate = new FileNodeDelegate(m_services, listView);
  listView->setItemDelegate(m_delegate);
  listView->setSelectionMode(QAbstractItemView::SingleSelection);
  listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
  listView->setDragEnabled(false);
  listView->setAcceptDrops(false);
  m_listView.reset(listView);

  connect(listView, &QListView::activated, this, &HistoryUnitedEntry::handleItemActivated);
}

void HistoryUnitedEntry::processInternal(
    const QString &p_args,
    const std::function<void(const QSharedPointer<QWidget> &)> &p_popupWidgetFunc) {
  setOngoing(true);

  m_model->loadHistory();
  m_proxyModel->setFilterFixedString(p_args.trimmed());

  if (m_model->rowCount() == 0) {
    p_popupWidgetFunc(EntryWidgetFactory::createLabel(tr("No recent files")));
    finish();
    return;
  }

  if (m_proxyModel->rowCount() == 0) {
    p_popupWidgetFunc(EntryWidgetFactory::createLabel(tr("No matching files")));
    finish();
    return;
  }

  if (auto *listView = qobject_cast<QListView *>(m_listView.data())) {
    listView->setCurrentIndex(m_proxyModel->index(0, 0));
  }

  p_popupWidgetFunc(m_listView);
  finish();
}

void HistoryUnitedEntry::finish() {
  setOngoing(false);
  emit finished();
}

QSharedPointer<QWidget> HistoryUnitedEntry::currentPopupWidget() const { return m_listView; }

void HistoryUnitedEntry::handleItemActivated(const QModelIndex &p_index) {
  if (!p_index.isValid()) {
    return;
  }

  const QModelIndex sourceIndex = m_proxyModel->mapToSource(p_index);
  NodeIdentifier nodeId = m_model->nodeIdFromIndex(sourceIndex);
  if (!nodeId.isValid()) {
    return;
  }

  FileOpenSettings settings;
  auto *bufferSvc = m_services.get<BufferService>();
  if (bufferSvc) {
    bufferSvc->openBuffer(nodeId, settings);
  }

  emit itemActivated(true, false);
}
