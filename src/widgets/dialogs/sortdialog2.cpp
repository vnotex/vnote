#include "sortdialog2.h"

#include <algorithm>

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

using namespace vnotex;

namespace {

// Object names — tests discover widgets via findChild<>(name) per
// src/widgets/dialogs/AGENTS.md "Test-discovery rule". These constants are
// part of the stable contract surface; renaming requires a test update.
// The list widget name is kept as-is across the QListWidget -> QTreeWidget
// swap on purpose: the object name is the contract, the type is not.
const char *kListWidgetName = "sortListWidget";
const char *kTopBtnName = "sortTopBtn";
const char *kUpBtnName = "sortUpBtn";
const char *kDownBtnName = "sortDownBtn";
const char *kBottomBtnName = "sortBottomBtn";

const int kColName = 0;
const int kColCreated = 1;
const int kColModified = 2;
const int kColumnCount = 3;

// The format string is resolved by the caller ONCE and passed in: building a
// QLocale::system() and re-querying dateTimeFormat() per cell would hit the
// system-locale backend twice for every row.
QString formatTimestamp(const QDateTime &p_dt, const QString &p_format) {
  if (!p_dt.isValid()) {
    return QString();
  }
  return p_dt.toLocalTime().toString(p_format);
}

// Sort key for a timestamp column: the raw UTC ms, or an invalid QVariant when
// the timestamp is unknown. Display formatting can therefore never influence
// ordering.
QVariant timestampKey(const QDateTime &p_dt) {
  if (!p_dt.isValid()) {
    return QVariant();
  }
  return QVariant(static_cast<qlonglong>(p_dt.toMSecsSinceEpoch()));
}

} // namespace

// Internal-move tree that reports MANUAL reorders. Header sorting and the move
// buttons also take/insert rows, so the model's rowsRemoved/rowsInserted cannot
// distinguish a user drag from a programmatic reorder — hence a dedicated
// signal raised from dropEvent() only. Drops ONTO an item are rejected so no
// row can ever gain a child.
class SortDialog2TreeWidget : public QTreeWidget {
  Q_OBJECT
public:
  explicit SortDialog2TreeWidget(QWidget *p_parent = nullptr) : QTreeWidget(p_parent) {}

signals:
  void manualOrderChanged();

protected:
  void dropEvent(QDropEvent *p_event) override {
    const auto pos = dropIndicatorPosition();
    if (pos == QAbstractItemView::OnItem) {
      p_event->ignore();
      return;
    }
    QTreeWidget::dropEvent(p_event);
    if (p_event->isAccepted()) {
      emit manualOrderChanged();
    }
  }
};

SortDialog2::SortDialog2(const QString &p_title, const QString &p_subtitle,
                         const QVector<Entry> &p_entries, QWidget *p_parent)
    : QDialog(p_parent) {
  m_collator = QCollator(QLocale::system());
  m_collator.setCaseSensitivity(Qt::CaseInsensitive);

  setupUi(p_title, p_subtitle);

  const QString dateTimeFormat = QLocale::system().dateTimeFormat(QLocale::ShortFormat);
  for (const auto &entry : p_entries) {
    auto *item = new QTreeWidgetItem();
    item->setText(kColName, entry.m_name);
    // The raw name lives in the role so getSortedOrder() is independent of any
    // future display decoration.
    item->setData(kColName, Qt::UserRole, entry.m_name);
    item->setText(kColCreated, formatTimestamp(entry.m_createdUtc, dateTimeFormat));
    item->setData(kColCreated, Qt::UserRole, timestampKey(entry.m_createdUtc));
    item->setText(kColModified, formatTimestamp(entry.m_modifiedUtc, dateTimeFormat));
    item->setData(kColModified, Qt::UserRole, timestampKey(entry.m_modifiedUtc));
    // Rows are draggable but never drop targets — only the invisible root is,
    // which is what a flat internal-move reorder needs.
    item->setFlags((item->flags() | Qt::ItemIsDragEnabled) & ~Qt::ItemIsDropEnabled);
    m_treeWidget->addTopLevelItem(item);
  }

  updateButtonsEnabled();
}

void SortDialog2::setupUi(const QString &p_title, const QString &p_subtitle) {
  setWindowTitle(p_title);

  auto *mainLayout = new QVBoxLayout(this);

  if (!p_subtitle.isEmpty()) {
    auto *subtitleLabel = new QLabel(p_subtitle, this);
    subtitleLabel->setWordWrap(true);
    mainLayout->addWidget(subtitleLabel);
  }

  auto *bodyLayout = new QHBoxLayout();
  mainLayout->addLayout(bodyLayout);

  // Reorderable rows. ExtendedSelection lets the user pick a contiguous
  // block; updateButtonsEnabled() disables the move buttons when the
  // selection is non-contiguous so the per-button move semantics remain
  // well-defined.
  auto *tree = new SortDialog2TreeWidget(this);
  m_treeWidget = tree;
  m_treeWidget->setObjectName(QLatin1String(kListWidgetName));
  m_treeWidget->setColumnCount(kColumnCount);
  m_treeWidget->setHeaderLabels({tr("Name"), tr("Created"), tr("Modified")});
  m_treeWidget->setRootIsDecorated(false);
  m_treeWidget->setUniformRowHeights(true);
  m_treeWidget->setAllColumnsShowFocus(true);
  m_treeWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_treeWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_treeWidget->setDragDropMode(QAbstractItemView::InternalMove);
  m_treeWidget->setDefaultDropAction(Qt::MoveAction);
  m_treeWidget->setDragDropOverwriteMode(false);
  // Explicit: the header click is a ONE-SHOT reorder. Live sorting would
  // silently defeat the move buttons.
  m_treeWidget->setSortingEnabled(false);
  // Flat-list reordering needs the invisible root to accept drops.
  m_treeWidget->invisibleRootItem()->setFlags(m_treeWidget->invisibleRootItem()->flags() |
                                              Qt::ItemIsDropEnabled);

  auto *header = m_treeWidget->header();
  header->setSectionsClickable(true);
  header->setSortIndicatorShown(true);
  header->setStretchLastSection(false);
  header->setSectionResizeMode(kColName, QHeaderView::Stretch);
  header->setSectionResizeMode(kColCreated, QHeaderView::ResizeToContents);
  header->setSectionResizeMode(kColModified, QHeaderView::ResizeToContents);
  header->setSortIndicator(-1, Qt::AscendingOrder);
  connect(header, &QHeaderView::sectionClicked, this, &SortDialog2::sortByColumn);

  bodyLayout->addWidget(m_treeWidget);

  auto *btnLayout = new QVBoxLayout();
  bodyLayout->addLayout(btnLayout);

  m_topBtn = new QPushButton(tr("Move to &Top"), this);
  m_topBtn->setObjectName(QLatin1String(kTopBtnName));
  connect(m_topBtn, &QPushButton::clicked, this, &SortDialog2::moveToTop);
  btnLayout->addWidget(m_topBtn);

  m_upBtn = new QPushButton(tr("Move &Up"), this);
  m_upBtn->setObjectName(QLatin1String(kUpBtnName));
  connect(m_upBtn, &QPushButton::clicked, this, &SortDialog2::moveUp);
  btnLayout->addWidget(m_upBtn);

  m_downBtn = new QPushButton(tr("Move &Down"), this);
  m_downBtn->setObjectName(QLatin1String(kDownBtnName));
  connect(m_downBtn, &QPushButton::clicked, this, &SortDialog2::moveDown);
  btnLayout->addWidget(m_downBtn);

  m_bottomBtn = new QPushButton(tr("Move to &Bottom"), this);
  m_bottomBtn->setObjectName(QLatin1String(kBottomBtnName));
  connect(m_bottomBtn, &QPushButton::clicked, this, &SortDialog2::moveToBottom);
  btnLayout->addWidget(m_bottomBtn);

  btnLayout->addStretch();

  auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  mainLayout->addWidget(buttonBox);

  // Keep buttons in sync with selection and with model changes (drag-drop
  // reorders rows by calling take/insert on the model directly).
  connect(m_treeWidget, &QTreeWidget::itemSelectionChanged, this,
          &SortDialog2::updateButtonsEnabled);
  connect(m_treeWidget->model(), &QAbstractItemModel::rowsMoved, this,
          [this]() { updateButtonsEnabled(); });
  connect(m_treeWidget->model(), &QAbstractItemModel::rowsInserted, this,
          [this]() { updateButtonsEnabled(); });
  connect(m_treeWidget->model(), &QAbstractItemModel::rowsRemoved, this,
          [this]() { updateButtonsEnabled(); });
  // A user drag is the only model change that clears the sort indicator on its
  // own; the move handlers call clearSortIndicator() directly.
  connect(tree, &SortDialog2TreeWidget::manualOrderChanged, this, &SortDialog2::clearSortIndicator);
}

QStringList SortDialog2::getSortedOrder() const {
  QStringList result;
  const int cnt = m_treeWidget->topLevelItemCount();
  result.reserve(cnt);
  for (int i = 0; i < cnt; ++i) {
    result.append(m_treeWidget->topLevelItem(i)->data(kColName, Qt::UserRole).toString());
  }
  return result;
}

void SortDialog2::clearSortIndicator() {
  m_lastSortColumn = -1;
  m_lastSortOrder = Qt::AscendingOrder;
  m_treeWidget->header()->setSortIndicator(-1, Qt::AscendingOrder);
}

void SortDialog2::sortByColumn(int p_column) {
  if (p_column < 0 || p_column >= kColumnCount) {
    return;
  }

  if (p_column == m_lastSortColumn) {
    m_lastSortOrder =
        (m_lastSortOrder == Qt::AscendingOrder) ? Qt::DescendingOrder : Qt::AscendingOrder;
  } else {
    m_lastSortOrder = Qt::AscendingOrder;
  }
  m_lastSortColumn = p_column;

  // Snapshot the selection by name so it survives the take/insert cycle.
  QSet<QString> selectedNames;
  const auto selected = m_treeWidget->selectedItems();
  for (auto *it : selected) {
    selectedNames.insert(it->data(kColName, Qt::UserRole).toString());
  }

  QList<QTreeWidgetItem *> items;
  items.reserve(m_treeWidget->topLevelItemCount());
  while (m_treeWidget->topLevelItemCount() > 0) {
    items.append(m_treeWidget->takeTopLevelItem(0));
  }

  const bool ascending = (m_lastSortOrder == Qt::AscendingOrder);
  if (p_column == kColName) {
    std::stable_sort(items.begin(), items.end(),
                     [this, ascending](QTreeWidgetItem *p_a, QTreeWidgetItem *p_b) {
                       const QString a = p_a->data(kColName, Qt::UserRole).toString();
                       const QString b = p_b->data(kColName, Qt::UserRole).toString();
                       int cmp = m_collator.compare(a, b);
                       if (cmp == 0) {
                         // Keep the order total: the collator is
                         // case-insensitive, so "alpha" and "Alpha" tie.
                         cmp = QString::compare(a, b, Qt::CaseSensitive);
                       }
                       return ascending ? (cmp < 0) : (cmp > 0);
                     });
  } else {
    std::stable_sort(items.begin(), items.end(),
                     [p_column, ascending](QTreeWidgetItem *p_a, QTreeWidgetItem *p_b) {
                       const QVariant va = p_a->data(p_column, Qt::UserRole);
                       const QVariant vb = p_b->data(p_column, Qt::UserRole);
                       const bool aValid = va.isValid();
                       const bool bValid = vb.isValid();
                       // An unknown timestamp is not "oldest": it sorts last in
                       // BOTH directions.
                       if (!aValid || !bValid) {
                         return aValid && !bValid;
                       }
                       const qlonglong a = va.toLongLong();
                       const qlonglong b = vb.toLongLong();
                       if (a == b) {
                         return false;
                       }
                       return ascending ? (a < b) : (a > b);
                     });
  }

  m_treeWidget->addTopLevelItems(items);
  m_treeWidget->header()->setSortIndicator(p_column, m_lastSortOrder);

  // Restore the selection by name. Build one QItemSelection and apply it in a
  // single call: a per-row setSelected() would emit itemSelectionChanged (and
  // therefore re-run the O(n) updateButtonsEnabled()) once per selected row.
  if (!selectedNames.isEmpty()) {
    QItemSelection selection;
    const int lastColumn = kColumnCount - 1;
    const int cnt = m_treeWidget->topLevelItemCount();
    for (int i = 0; i < cnt; ++i) {
      auto *it = m_treeWidget->topLevelItem(i);
      if (selectedNames.contains(it->data(kColName, Qt::UserRole).toString())) {
        // Not indexFromItem(): it is protected in Qt 5. These are all top-level
        // rows, so the model index is addressable directly.
        const QModelIndex idx = m_treeWidget->model()->index(i, kColName);
        selection.select(idx, idx.sibling(idx.row(), lastColumn));
      }
    }
    if (!selection.isEmpty()) {
      m_treeWidget->selectionModel()->select(selection, QItemSelectionModel::Select |
                                                            QItemSelectionModel::Rows);
    }
  }
  updateButtonsEnabled();
}

int SortDialog2::selectedRowCount() const {
  return m_treeWidget->selectionModel()->selectedRows().size();
}

void SortDialog2::selectedRowRange(int &p_first, int &p_last) const {
  const auto rows = m_treeWidget->selectionModel()->selectedRows();
  if (rows.isEmpty()) {
    p_first = -1;
    p_last = -1;
    return;
  }
  p_first = m_treeWidget->topLevelItemCount();
  p_last = -1;
  for (const auto &idx : rows) {
    const int r = idx.row();
    if (r < p_first)
      p_first = r;
    if (r > p_last)
      p_last = r;
  }
}

void SortDialog2::moveToTop() {
  int first = -1, last = -1;
  selectedRowRange(first, last);
  if (first < 0)
    return;
  // Refuse non-contiguous selection (matches button-disabled state but also
  // guards against keyboard shortcuts that bypass the button).
  if ((last - first + 1) != selectedRowCount())
    return;
  if (first == 0)
    return;

  m_treeWidget->clearSelection();
  // Take from `last` and insert at 0 repeatedly so the block ends up at
  // [0..last-first] in its original order.
  for (int i = last - first; i >= 0; --i) {
    QTreeWidgetItem *item = m_treeWidget->takeTopLevelItem(last);
    m_treeWidget->insertTopLevelItem(0, item);
    item->setSelected(true);
  }
  m_treeWidget->setCurrentItem(m_treeWidget->topLevelItem(0), 0, QItemSelectionModel::NoUpdate);
  clearSortIndicator();
  updateButtonsEnabled();
}

void SortDialog2::moveUp() {
  int first = -1, last = -1;
  selectedRowRange(first, last);
  if (first < 0)
    return;
  if ((last - first + 1) != selectedRowCount())
    return;
  if (first == 0)
    return;

  m_treeWidget->clearSelection();
  // Take from `last` and insert at first-1 repeatedly. The block ends up at
  // [first-1..last-1] in its original order.
  for (int i = last - first; i >= 0; --i) {
    QTreeWidgetItem *item = m_treeWidget->takeTopLevelItem(last);
    m_treeWidget->insertTopLevelItem(first - 1, item);
    item->setSelected(true);
  }
  m_treeWidget->setCurrentItem(m_treeWidget->topLevelItem(first - 1), 0,
                               QItemSelectionModel::NoUpdate);
  clearSortIndicator();
  updateButtonsEnabled();
}

void SortDialog2::moveDown() {
  int first = -1, last = -1;
  selectedRowRange(first, last);
  if (first < 0)
    return;
  if ((last - first + 1) != selectedRowCount())
    return;
  if (last == m_treeWidget->topLevelItemCount() - 1)
    return;

  m_treeWidget->clearSelection();
  // Take from `first` and insert at last+1 repeatedly. The block ends up at
  // [first+1..last+1] in its original order.
  for (int i = last - first; i >= 0; --i) {
    QTreeWidgetItem *item = m_treeWidget->takeTopLevelItem(first);
    m_treeWidget->insertTopLevelItem(last + 1, item);
    item->setSelected(true);
  }
  m_treeWidget->setCurrentItem(m_treeWidget->topLevelItem(first + 1), 0,
                               QItemSelectionModel::NoUpdate);
  clearSortIndicator();
  updateButtonsEnabled();
}

void SortDialog2::moveToBottom() {
  int first = -1, last = -1;
  selectedRowRange(first, last);
  if (first < 0)
    return;
  if ((last - first + 1) != selectedRowCount())
    return;
  if (last == m_treeWidget->topLevelItemCount() - 1)
    return;

  m_treeWidget->clearSelection();
  // Take from `first` and append, repeatedly. The block ends up at the end
  // of the list in its original order.
  for (int i = last - first; i >= 0; --i) {
    QTreeWidgetItem *item = m_treeWidget->takeTopLevelItem(first);
    m_treeWidget->addTopLevelItem(item);
    item->setSelected(true);
  }
  m_treeWidget->setCurrentItem(m_treeWidget->topLevelItem(m_treeWidget->topLevelItemCount() - 1), 0,
                               QItemSelectionModel::NoUpdate);
  clearSortIndicator();
  updateButtonsEnabled();
}

void SortDialog2::updateButtonsEnabled() {
  const int selectedCount = selectedRowCount();
  if (selectedCount == 0) {
    m_topBtn->setEnabled(false);
    m_upBtn->setEnabled(false);
    m_downBtn->setEnabled(false);
    m_bottomBtn->setEnabled(false);
    return;
  }

  int first = -1, last = -1;
  selectedRowRange(first, last);
  const bool contiguous = (last - first + 1) == selectedCount;
  if (!contiguous) {
    m_topBtn->setEnabled(false);
    m_upBtn->setEnabled(false);
    m_downBtn->setEnabled(false);
    m_bottomBtn->setEnabled(false);
    return;
  }

  const int total = m_treeWidget->topLevelItemCount();
  const bool atTop = (first == 0);
  const bool atBottom = (last == total - 1);
  m_topBtn->setEnabled(!atTop);
  m_upBtn->setEnabled(!atTop);
  m_downBtn->setEnabled(!atBottom);
  m_bottomBtn->setEnabled(!atBottom);
}

#include "sortdialog2.moc"
