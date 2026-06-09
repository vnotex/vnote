#include "opennotebookdialog2.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <controllers/opennotebookcontroller.h>
#include <core/servicelocator.h>

#include "../locationinputwithbrowsebutton.h"

using namespace vnotex;

namespace {

// QObject names that tests use to discover widgets via findChild<>().
const char *const kLocalModeRadioName = "localModeRadio";
const char *const kRemoteModeRadioName = "remoteModeRadio";
const char *const kModeStackName = "modeStack";
const char *const kLocalRootInputName = "localRootInput";
const char *const kRemoteUrlEditName = "remoteUrlEdit";
const char *const kRemotePatEditName = "remotePatEdit";
const char *const kRemoteDestEditName = "remoteDestEdit";
const char *const kRemoteDestBrowseButtonName = "remoteDestBrowseButton";
const char *const kProgressBarName = "openNotebookProgressBar";
const char *const kOpenButtonName = "openButton";
const char *const kCancelButtonName = "cancelButton";

// Single source of truth for the URL-scheme guard. T22's
// OpenNotebookController::validateCloneInput() MUST mirror this regex so the
// dialog and controller agree on what is acceptable. Accepts:
//   * https://...
//   * file:///...   (note: three slashes; covers Windows/POSIX file URLs)
// Rejects everything else (ssh, http, git, scp-like, bare paths, ...).
const char *const kRemoteUrlSchemeRegex = "^(https://|file:///)\\S+$";

} // namespace

OpenNotebookDialog2::OpenNotebookDialog2(ServiceLocator &p_services, QWidget *p_parent)
    : ScrollDialog(p_parent), m_services(p_services) {
  m_controller = new OpenNotebookController(m_services, this);

  setupUI();

  // Application-modal (NOT system-modal). exec() defaults to this, but be
  // explicit per the project convention for modal dialogs.
  setWindowModality(Qt::ApplicationModal);

  // Initialize button + validation state.
  updateOpenButtonState();
}

OpenNotebookDialog2::~OpenNotebookDialog2() = default;

void OpenNotebookDialog2::setupUI() {
  auto *mainWidget = new QWidget(this);
  auto *mainLayout = new QVBoxLayout(mainWidget);

  // Top: mode selector radios.
  auto *modeRow = new QHBoxLayout();
  m_localModeRadio = new QRadioButton(tr("Local Folder"), mainWidget);
  m_localModeRadio->setObjectName(QLatin1String(kLocalModeRadioName));
  m_localModeRadio->setToolTip(tr("Open an existing VNote notebook from a local folder on disk."));
  m_localModeRadio->setChecked(true);

  m_remoteModeRadio = new QRadioButton(tr("Remote URL"), mainWidget);
  m_remoteModeRadio->setObjectName(QLatin1String(kRemoteModeRadioName));
  m_remoteModeRadio->setToolTip(
      tr("Clone a VNote notebook from a remote git URL or a file:// path."));

  m_modeGroup = new QButtonGroup(this);
  m_modeGroup->setExclusive(true);
  m_modeGroup->addButton(m_localModeRadio, static_cast<int>(LocalMode));
  m_modeGroup->addButton(m_remoteModeRadio, static_cast<int>(RemoteMode));

  modeRow->addWidget(m_localModeRadio);
  modeRow->addWidget(m_remoteModeRadio);
  modeRow->addStretch();
  mainLayout->addLayout(modeRow);

  // Middle: stacked mode-specific area.
  m_modeStack = new QStackedWidget(mainWidget);
  m_modeStack->setObjectName(QLatin1String(kModeStackName));

  m_localPage = new QWidget(m_modeStack);
  setupLocalPage(m_localPage);
  m_modeStack->addWidget(m_localPage);

  m_remotePage = new QWidget(m_modeStack);
  setupRemotePage(m_remotePage);
  m_modeStack->addWidget(m_remotePage);

  m_modeStack->setCurrentIndex(static_cast<int>(LocalMode));
  mainLayout->addWidget(m_modeStack);

  // Bottom: progress bar (hidden by default; T22 will show during clone).
  m_progressBar = new QProgressBar(mainWidget);
  m_progressBar->setObjectName(QLatin1String(kProgressBarName));
  m_progressBar->setRange(0, 0); // indeterminate by default
  m_progressBar->hide();
  mainLayout->addWidget(m_progressBar);

  setCentralWidget(mainWidget);

  // Dialog buttons: Open (accept role) + Cancel (reject role).
  setDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel);
  if (auto *box = getDialogButtonBox()) {
    if (auto *openBtn = box->button(QDialogButtonBox::Open)) {
      openBtn->setObjectName(QLatin1String(kOpenButtonName));
      openBtn->setText(tr("Open"));
      openBtn->setDefault(true);
    }
    if (auto *cancelBtn = box->button(QDialogButtonBox::Cancel)) {
      cancelBtn->setObjectName(QLatin1String(kCancelButtonName));
    }
  }
  setButtonEnabled(QDialogButtonBox::Open, false);

  setWindowTitle(tr("Open Notebook"));

  // Wire mode toggle.
  connect(m_localModeRadio, &QRadioButton::toggled, this, &OpenNotebookDialog2::onModeChanged);
  connect(m_remoteModeRadio, &QRadioButton::toggled, this, &OpenNotebookDialog2::onModeChanged);
}

void OpenNotebookDialog2::setupLocalPage(QWidget *p_page) {
  auto *layout = new QFormLayout(p_page);

  m_localRootInput = new LocationInputWithBrowseButton(p_page);
  m_localRootInput->setObjectName(QLatin1String(kLocalRootInputName));
  m_localRootInput->setBrowseType(LocationInputWithBrowseButton::Folder,
                                  tr("Select Notebook Root Folder"));
  m_localRootInput->setPlaceholderText(tr("Select the root folder of an existing VNote notebook"));
  m_localRootInput->setToolTip(
      tr("Root folder of an existing VNote notebook (must contain a valid notebook config)."));
  layout->addRow(tr("Root folder path:"), m_localRootInput);

  connect(m_localRootInput, &LocationInputWithBrowseButton::textChanged, this,
          &OpenNotebookDialog2::onLocalRootChanged);
}

void OpenNotebookDialog2::setupRemotePage(QWidget *p_page) {
  auto *layout = new QFormLayout(p_page);

  // Remote URL field.
  m_remoteUrlEdit = new QLineEdit(p_page);
  m_remoteUrlEdit->setObjectName(QLatin1String(kRemoteUrlEditName));
  m_remoteUrlEdit->setPlaceholderText(
      tr("https://github.com/user/repo.git  or  file:///path/to/repo.git"));
  m_remoteUrlEdit->setToolTip(tr("Remote git URL. Only HTTPS and file:// schemes are supported."));
  layout->addRow(tr("Remote URL (HTTPS or file://):"), m_remoteUrlEdit);

  // PAT field (password echo).
  m_remotePatEdit = new QLineEdit(p_page);
  m_remotePatEdit->setObjectName(QLatin1String(kRemotePatEditName));
  m_remotePatEdit->setEchoMode(QLineEdit::Password);
  m_remotePatEdit->setPlaceholderText(tr("Leave blank to open the notebook read-only"));
  m_remotePatEdit->setToolTip(
      tr("If empty, the notebook opens read-only (you cannot edit, only view)."));
  layout->addRow(tr("Personal Access Token (optional):"), m_remotePatEdit);

  // Destination folder: read-only QLineEdit + Browse button.
  auto *destContainer = new QWidget(p_page);
  auto *destLayout = new QHBoxLayout(destContainer);
  destLayout->setContentsMargins(0, 0, 0, 0);

  m_remoteDestEdit = new QLineEdit(destContainer);
  m_remoteDestEdit->setObjectName(QLatin1String(kRemoteDestEditName));
  m_remoteDestEdit->setReadOnly(true);
  m_remoteDestEdit->setPlaceholderText(
      tr("Browse to pick a parent folder; the leaf name is filled in from the URL"));

  m_remoteDestBrowseButton = new QPushButton(tr("Browse..."), destContainer);
  m_remoteDestBrowseButton->setObjectName(QLatin1String(kRemoteDestBrowseButtonName));

  destLayout->addWidget(m_remoteDestEdit, 1);
  destLayout->addWidget(m_remoteDestBrowseButton);

  layout->addRow(tr("Destination folder:"), destContainer);

  // Hint labels: spelled-out semantics for the must-not-exist + read-only-on-empty-PAT rules.
  m_remoteDestHintLabel =
      new QLabel(tr("The destination folder must not yet exist; it will be created."), p_page);
  m_remoteDestHintLabel->setWordWrap(true);
  layout->addRow(QString(), m_remoteDestHintLabel);

  m_remotePatHintLabel = new QLabel(
      tr("Note: empty PAT means the notebook opens read-only (you cannot edit, only view)."),
      p_page);
  m_remotePatHintLabel->setWordWrap(true);
  layout->addRow(QString(), m_remotePatHintLabel);

  // Wire field-change validation.
  connect(m_remoteUrlEdit, &QLineEdit::textChanged, this, [this](const QString &) {
    updateSuggestedDestination();
    onRemoteFieldsChanged();
  });
  connect(m_remotePatEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { onRemoteFieldsChanged(); });
  connect(m_remoteDestEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { onRemoteFieldsChanged(); });
  connect(m_remoteDestBrowseButton, &QPushButton::clicked, this,
          &OpenNotebookDialog2::onBrowseRemoteDestClicked);
}

OpenNotebookDialog2::Mode OpenNotebookDialog2::currentMode() const {
  return m_remoteModeRadio && m_remoteModeRadio->isChecked() ? RemoteMode : LocalMode;
}

void OpenNotebookDialog2::onModeChanged() {
  const Mode mode = currentMode();
  m_modeStack->setCurrentIndex(static_cast<int>(mode));
  // Clear stale validation text from the prior mode.
  setInformationText(QString(), InformationLevel::Info);
  updateOpenButtonState();
}

void OpenNotebookDialog2::onLocalRootChanged() {
  if (currentMode() != LocalMode) {
    return;
  }
  updateOpenButtonState();
}

void OpenNotebookDialog2::onRemoteFieldsChanged() {
  if (currentMode() != RemoteMode) {
    return;
  }
  updateOpenButtonState();
}

void OpenNotebookDialog2::onBrowseRemoteDestClicked() {
  QString initDir = m_remoteSelectedParentDir;
  if (initDir.isEmpty()) {
    initDir = QDir::homePath();
  }
  const QString picked =
      QFileDialog::getExistingDirectory(this, tr("Select Destination Parent Folder"), initDir);
  if (picked.isEmpty()) {
    return;
  }
  m_remoteSelectedParentDir = picked;
  updateSuggestedDestination();
}

void OpenNotebookDialog2::updateSuggestedDestination() {
  if (m_remoteSelectedParentDir.isEmpty()) {
    return;
  }
  const QString leaf = extractRepoNameFromUrl(m_remoteUrlEdit->text());
  if (leaf.isEmpty()) {
    // No usable leaf yet (URL empty or only-scheme). Park parent in the field
    // so the user sees what they picked, but mark with a trailing slash so
    // validation flags it as "not a final destination yet".
    m_remoteDestEdit->setText(QDir::cleanPath(m_remoteSelectedParentDir));
    return;
  }
  const QString full = QDir::cleanPath(m_remoteSelectedParentDir + QLatin1Char('/') + leaf);
  m_remoteDestEdit->setText(full);
}

void OpenNotebookDialog2::updateOpenButtonState() {
  if (currentMode() == LocalMode) {
    const QString path = m_localRootInput ? m_localRootInput->text().trimmed() : QString();
    if (path.isEmpty()) {
      setInformationText(QString(), InformationLevel::Info);
      setButtonEnabled(QDialogButtonBox::Open, false);
      return;
    }
    OpenNotebookValidationResult validation = m_controller->validateRootFolder(path);
    if (!validation.valid) {
      setInformationText(validation.message, InformationLevel::Error);
      setButtonEnabled(QDialogButtonBox::Open, false);
      return;
    }
    setInformationText(QString(), InformationLevel::Info);
    setButtonEnabled(QDialogButtonBox::Open, true);
    return;
  }

  // Remote mode.
  const RemoteValidation remote = validateRemoteInputs();
  if (!remote.valid) {
    setInformationText(remote.message, InformationLevel::Error);
    setButtonEnabled(QDialogButtonBox::Open, false);
    return;
  }
  setInformationText(QString(), InformationLevel::Info);
  setButtonEnabled(QDialogButtonBox::Open, true);
}

OpenNotebookDialog2::RemoteValidation OpenNotebookDialog2::validateRemoteInputs() const {
  RemoteValidation result;
  const QString url = m_remoteUrlEdit ? m_remoteUrlEdit->text().trimmed() : QString();
  if (url.isEmpty()) {
    // Empty URL: keep the feedback area quiet until the user types.
    return result;
  }

  static const QRegularExpression scheme(QString::fromLatin1(kRemoteUrlSchemeRegex));
  if (!scheme.match(url).hasMatch()) {
    result.message = tr("Remote URL must use HTTPS or file:// scheme (got: %1).").arg(url);
    return result;
  }

  const QString dest = m_remoteDestEdit ? m_remoteDestEdit->text().trimmed() : QString();
  if (dest.isEmpty()) {
    result.message = tr("Click Browse... to pick a destination parent folder.");
    return result;
  }

  // The dialog appends the URL-derived leaf onto the parent. A bare parent
  // (no leaf appended yet) means we have no usable repo name to derive.
  const QString leaf = extractRepoNameFromUrl(url);
  if (leaf.isEmpty()) {
    result.message = tr("Could not derive a repository name from the URL; please refine the URL.");
    return result;
  }

  if (QFileInfo::exists(dest)) {
    result.message = tr("The destination folder (%1) already exists; it must not exist.").arg(dest);
    return result;
  }

  result.valid = true;
  return result;
}

QString OpenNotebookDialog2::extractRepoNameFromUrl(const QString &p_url) {
  QString trimmed = p_url.trimmed();
  if (trimmed.isEmpty()) {
    return QString();
  }
  // Strip trailing slashes.
  while (trimmed.endsWith(QLatin1Char('/'))) {
    trimmed.chop(1);
  }
  // Strip trailing ".git" (case-insensitive).
  if (trimmed.endsWith(QStringLiteral(".git"), Qt::CaseInsensitive)) {
    trimmed.chop(4);
  }
  // Strip any further trailing slash after the .git removal.
  while (trimmed.endsWith(QLatin1Char('/'))) {
    trimmed.chop(1);
  }
  const int slash = trimmed.lastIndexOf(QLatin1Char('/'));
  if (slash < 0) {
    return QString();
  }
  const QString leaf = trimmed.mid(slash + 1);
  // Defensive: reject obviously-bogus leaves (e.g. ":" left over from a
  // malformed URL). Anything left over is fine to use as a folder name.
  if (leaf.isEmpty()) {
    return QString();
  }
  return leaf;
}

void OpenNotebookDialog2::acceptedButtonClicked() {
  if (currentMode() == LocalMode) {
    handleLocalOpen();
  } else {
    handleRemoteOpenStubbed();
  }
}

void OpenNotebookDialog2::handleLocalOpen() {
  OpenNotebookInput input;
  input.rootFolderPath = m_localRootInput->text().trimmed();

  OpenNotebookResult result = m_controller->openNotebook(input);
  if (!result.success) {
    setInformationText(result.errorMessage, InformationLevel::Error);
    QMessageBox::critical(this, tr("Open Notebook Failed"), result.errorMessage);
    return;
  }

  m_openedNotebookId = result.notebookId;
  emit notebookOpened(m_openedNotebookId);
  accept();
}

void OpenNotebookDialog2::handleRemoteOpenStubbed() {
  // T24 STUB: the actual clone path is wired in T25 (NotebookExplorer2
  // wiring) after T22 supplies cloneAndOpen() / cloneFinished /
  // cloneProgressUpdated. Show an informational message so the user knows
  // why nothing happened; the dialog stays open.
  QMessageBox::information(this, tr("Remote Open"), tr("Remote clone will be wired in task T25."));
}

QString OpenNotebookDialog2::getOpenedNotebookId() const { return m_openedNotebookId; }
