#include "sortdialog2.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

using namespace vnotex;

namespace {

// Object names — tests discover widgets via findChild<>(name) per
// src/widgets/dialogs/AGENTS.md "Test-discovery rule". These constants are
// part of the stable contract surface; renaming requires a test update.
const char *kListWidgetName = "sortListWidget";
const char *kTopBtnName = "sortTopBtn";
const char *kUpBtnName = "sortUpBtn";
const char *kDownBtnName = "sortDownBtn";
const char *kBottomBtnName = "sortBottomBtn";

} // namespace

SortDialog2::SortDialog2(const QString &p_title, const QString &p_subtitle,
                         const QStringList &p_initialOrder, QWidget *p_parent)
    : QDialog(p_parent) {
  setupUi(p_title, p_subtitle);

  for (const auto &name : p_initialOrder) {
    m_listWidget->addItem(name);
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

  // Reorderable list. ExtendedSelection lets the user pick a contiguous
  // block; updateButtonsEnabled() disables the move buttons when the
  // selection is non-contiguous so the per-button move semantics remain
  // well-defined.
  m_listWidget = new QListWidget(this);
  m_listWidget->setObjectName(QLatin1String(kListWidgetName));
  m_listWidget->setDragDropMode(QAbstractItemView::InternalMove);
  m_listWidget->setDefaultDropAction(Qt::MoveAction);
  m_listWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
  bodyLayout->addWidget(m_listWidget);

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
  // reorders rows by calling takeItem/insertItem on the model directly).
  connect(m_listWidget, &QListWidget::itemSelectionChanged, this,
          &SortDialog2::updateButtonsEnabled);
  connect(m_listWidget->model(), &QAbstractItemModel::rowsMoved, this,
          [this]() { updateButtonsEnabled(); });
  connect(m_listWidget->model(), &QAbstractItemModel::rowsInserted, this,
          [this]() { updateButtonsEnabled(); });
  connect(m_listWidget->model(), &QAbstractItemModel::rowsRemoved, this,
          [this]() { updateButtonsEnabled(); });
}

QStringList SortDialog2::getSortedOrder() const {
  QStringList result;
  const int cnt = m_listWidget->count();
  result.reserve(cnt);
  for (int i = 0; i < cnt; ++i) {
    result.append(m_listWidget->item(i)->text());
  }
  return result;
}

void SortDialog2::selectedRowRange(int &p_first, int &p_last) const {
  const auto items = m_listWidget->selectedItems();
  if (items.isEmpty()) {
    p_first = -1;
    p_last = -1;
    return;
  }
  p_first = m_listWidget->count();
  p_last = -1;
  for (auto *it : items) {
    const int idx = m_listWidget->row(it);
    if (idx < p_first)
      p_first = idx;
    if (idx > p_last)
      p_last = idx;
  }
}

void SortDialog2::moveToTop() {
  int first = -1, last = -1;
  selectedRowRange(first, last);
  if (first < 0)
    return;
  // Refuse non-contiguous selection (matches button-disabled state but also
  // guards against keyboard shortcuts that bypass the button).
  if ((last - first + 1) != m_listWidget->selectedItems().size())
    return;
  if (first == 0)
    return;

  m_listWidget->clearSelection();
  // Take from `last` and insert at 0 repeatedly so the block ends up at
  // [0..last-first] in its original order.
  for (int i = last - first; i >= 0; --i) {
    QListWidgetItem *item = m_listWidget->takeItem(last);
    m_listWidget->insertItem(0, item);
    item->setSelected(true);
  }
  m_listWidget->setCurrentRow(0);
  updateButtonsEnabled();
}

void SortDialog2::moveUp() {
  int first = -1, last = -1;
  selectedRowRange(first, last);
  if (first < 0)
    return;
  if ((last - first + 1) != m_listWidget->selectedItems().size())
    return;
  if (first == 0)
    return;

  m_listWidget->clearSelection();
  // Take from `last` and insert at first-1 repeatedly. The block ends up at
  // [first-1..last-1] in its original order.
  for (int i = last - first; i >= 0; --i) {
    QListWidgetItem *item = m_listWidget->takeItem(last);
    m_listWidget->insertItem(first - 1, item);
    item->setSelected(true);
  }
  m_listWidget->setCurrentRow(first - 1);
  updateButtonsEnabled();
}

void SortDialog2::moveDown() {
  int first = -1, last = -1;
  selectedRowRange(first, last);
  if (first < 0)
    return;
  if ((last - first + 1) != m_listWidget->selectedItems().size())
    return;
  if (last == m_listWidget->count() - 1)
    return;

  m_listWidget->clearSelection();
  // Take from `first` and insert at last+1 repeatedly. The block ends up at
  // [first+1..last+1] in its original order.
  for (int i = last - first; i >= 0; --i) {
    QListWidgetItem *item = m_listWidget->takeItem(first);
    m_listWidget->insertItem(last + 1, item);
    item->setSelected(true);
  }
  m_listWidget->setCurrentRow(first + 1);
  updateButtonsEnabled();
}

void SortDialog2::moveToBottom() {
  int first = -1, last = -1;
  selectedRowRange(first, last);
  if (first < 0)
    return;
  if ((last - first + 1) != m_listWidget->selectedItems().size())
    return;
  if (last == m_listWidget->count() - 1)
    return;

  m_listWidget->clearSelection();
  // Take from `first` and append, repeatedly. The block ends up at the end
  // of the list in its original order.
  for (int i = last - first; i >= 0; --i) {
    QListWidgetItem *item = m_listWidget->takeItem(first);
    m_listWidget->addItem(item);
    item->setSelected(true);
  }
  m_listWidget->setCurrentRow(m_listWidget->count() - 1);
  updateButtonsEnabled();
}

void SortDialog2::updateButtonsEnabled() {
  const auto selected = m_listWidget->selectedItems();
  if (selected.isEmpty()) {
    m_topBtn->setEnabled(false);
    m_upBtn->setEnabled(false);
    m_downBtn->setEnabled(false);
    m_bottomBtn->setEnabled(false);
    return;
  }

  int first = -1, last = -1;
  selectedRowRange(first, last);
  const bool contiguous = (last - first + 1) == selected.size();
  if (!contiguous) {
    m_topBtn->setEnabled(false);
    m_upBtn->setEnabled(false);
    m_downBtn->setEnabled(false);
    m_bottomBtn->setEnabled(false);
    return;
  }

  const int total = m_listWidget->count();
  const bool atTop = (first == 0);
  const bool atBottom = (last == total - 1);
  m_topBtn->setEnabled(!atTop);
  m_upBtn->setEnabled(!atTop);
  m_downBtn->setEnabled(!atBottom);
  m_bottomBtn->setEnabled(!atBottom);
}
