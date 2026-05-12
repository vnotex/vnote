#include "syncconflictdialog2.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QStringLiteral>
#include <QVBoxLayout>
#include <QWidget>

namespace vnotex {

class ServiceLocator;

} // namespace vnotex

using namespace vnotex;

namespace {

// QObject names that tests use to discover widgets via findChild<>(). Kept in
// sync with the documentation in the dialog header and the plan acceptance
// criteria for T12.
const char *const kScrollAreaName = "conflictScrollArea";
const char *const kOkButtonName = "okButton";
const char *const kCancelButtonName = "cancelButton";

// Per-row objectName builders. The suffix scheme is documented in the plan
// (T12 acceptance criteria).
inline QString rowObjectName(int p_index) { return QStringLiteral("conflictRow_%1").arg(p_index); }

inline QString radioObjectName(int p_index, const char *p_suffix) {
  return QStringLiteral("radio_%1_%2").arg(p_index).arg(QLatin1String(p_suffix));
}

// Resolution string constants. These values match what vxcore expects for the
// per-file resolve_conflict call in T13.
const char *const kKeepLocal = "keep_local";
const char *const kKeepRemote = "keep_remote";
const char *const kKeepBoth = "keep_both";

} // namespace

SyncConflictDialog2::SyncConflictDialog2(ServiceLocator &p_services, const QString &p_notebookId,
                                         const QStringList &p_conflictFiles, QWidget *p_parent)
    : QDialog(p_parent), m_services(p_services), m_notebookId(p_notebookId),
      m_conflictFiles(p_conflictFiles) {
  Q_UNUSED(m_services);
  Q_UNUSED(m_notebookId);

  setupUI();
}

void SyncConflictDialog2::setupUI() {
  auto *rootLayout = new QVBoxLayout(this);

  // Header label - explains why the dialog is open and how many files are
  // affected.
  auto *headerLabel =
      new QLabel(tr("Sync conflict detected for %1 file(s). Choose how to resolve each:")
                     .arg(m_conflictFiles.size()),
                 this);
  headerLabel->setWordWrap(true);
  rootLayout->addWidget(headerLabel);

  // Scroll area hosting the per-file row stack. The container widget owns the
  // row layout; we let the scroll area resize the inner widget so long file
  // path labels do not force horizontal scrolling.
  m_scrollArea = new QScrollArea(this);
  m_scrollArea->setObjectName(QString::fromLatin1(kScrollAreaName));
  m_scrollArea->setWidgetResizable(true);

  auto *content = new QWidget(m_scrollArea);
  auto *contentLayout = new QVBoxLayout(content);
  contentLayout->setContentsMargins(0, 0, 0, 0);

  m_groups.reserve(m_conflictFiles.size());

  const QFont monospaceFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);

  for (int i = 0; i < m_conflictFiles.size(); ++i) {
    const QString &filePath = m_conflictFiles[i];

    auto *row = new QWidget(content);
    row->setObjectName(rowObjectName(i));

    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);

    // File path label - left aligned, monospaced for legibility, takes the
    // remaining horizontal space so the radio cluster sits flush right.
    auto *pathLabel = new QLabel(filePath, row);
    pathLabel->setFont(monospaceFont);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pathLabel->setToolTip(filePath);
    rowLayout->addWidget(pathLabel, /*stretch=*/1);

    // Radios: Keep Local (default) | Keep Remote | Keep Both.
    auto *localRb = new QRadioButton(tr("Keep Local"), row);
    localRb->setObjectName(radioObjectName(i, "local"));
    localRb->setChecked(true);

    auto *remoteRb = new QRadioButton(tr("Keep Remote"), row);
    remoteRb->setObjectName(radioObjectName(i, "remote"));

    auto *bothRb = new QRadioButton(tr("Keep Both"), row);
    bothRb->setObjectName(radioObjectName(i, "both"));

    rowLayout->addWidget(localRb);
    rowLayout->addWidget(remoteRb);
    rowLayout->addWidget(bothRb);

    // Group the three radios for mutual exclusivity. QButtonGroup is parented
    // to the dialog so its lifetime tracks the dialog (rather than the row
    // QWidget which is itself owned by the scroll-area content).
    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    group->addButton(localRb);
    group->addButton(remoteRb);
    group->addButton(bothRb);

    m_groups.append(group);
    contentLayout->addWidget(row);
  }

  // Push rows to the top so a small list does not center-float in a tall
  // dialog.
  contentLayout->addStretch(1);

  m_scrollArea->setWidget(content);
  rootLayout->addWidget(m_scrollArea, /*stretch=*/1);

  // Standard OK/Cancel button box. ObjectNames let tests click via
  // findChild<QPushButton*>("okButton"/"cancelButton").
  auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  if (auto *okBtn = buttonBox->button(QDialogButtonBox::Ok)) {
    okBtn->setObjectName(QString::fromLatin1(kOkButtonName));
  }
  if (auto *cancelBtn = buttonBox->button(QDialogButtonBox::Cancel)) {
    cancelBtn->setObjectName(QString::fromLatin1(kCancelButtonName));
  }
  rootLayout->addWidget(buttonBox);

  // Cancel rejects WITHOUT emitting resolutionsChosen (per ADR for T12).
  connect(buttonBox, &QDialogButtonBox::rejected, this, &SyncConflictDialog2::reject);
  // Routed through onAccepted() so we can build + emit the resolutions map
  // before close.
  connect(buttonBox, &QDialogButtonBox::accepted, this, &SyncConflictDialog2::onAccepted);

  setWindowTitle(tr("Resolve Sync Conflicts"));
}

QHash<QString, QString> SyncConflictDialog2::resolutions() const {
  QHash<QString, QString> map;
  map.reserve(m_groups.size());

  for (int i = 0; i < m_groups.size() && i < m_conflictFiles.size(); ++i) {
    auto *group = m_groups.at(i);
    if (!group) {
      continue;
    }
    auto *checked = group->checkedButton();
    if (!checked) {
      // Should be unreachable - localRb is checked by default in setupUI()
      // and the group is exclusive. Defensive: skip rather than insert empty.
      continue;
    }

    const QString radioName = checked->objectName();
    QString resolution;
    if (radioName.endsWith(QLatin1String("_local"))) {
      resolution = QString::fromLatin1(kKeepLocal);
    } else if (radioName.endsWith(QLatin1String("_remote"))) {
      resolution = QString::fromLatin1(kKeepRemote);
    } else if (radioName.endsWith(QLatin1String("_both"))) {
      resolution = QString::fromLatin1(kKeepBoth);
    } else {
      continue;
    }

    map.insert(m_conflictFiles.at(i), resolution);
  }

  return map;
}

void SyncConflictDialog2::onAccepted() {
  emit resolutionsChosen(resolutions());
  accept();
}
