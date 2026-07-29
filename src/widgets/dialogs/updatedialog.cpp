#include "updatedialog.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

using namespace vnotex;

UpdateDialog::UpdateDialog(const UpdateInfo &p_info, QWidget *p_parent)
    : QDialog(p_parent), m_info(p_info) {
  setupUi();
}

QString UpdateDialog::formatBytes(qint64 p_bytes) {
  if (p_bytes <= 0) {
    return QStringLiteral("-");
  }
  constexpr qint64 kMiB = 1024 * 1024;
  if (p_bytes >= kMiB) {
    return tr("%1 MB").arg(QString::number(static_cast<double>(p_bytes) / kMiB, 'f', 1));
  }
  return tr("%1 KB").arg(QString::number(static_cast<double>(p_bytes) / 1024, 'f', 1));
}

void UpdateDialog::setupUi() {
  setWindowTitle(tr("Check for Updates"));
  setModal(true);

  auto *layout = new QVBoxLayout(this);

  m_headline = new QLabel(this);
  QFont headlineFont = m_headline->font();
  headlineFont.setBold(true);
  m_headline->setFont(headlineFont);
  layout->addWidget(m_headline);

  m_summary = new QLabel(this);
  m_summary->setWordWrap(true);
  m_summary->setTextInteractionFlags(Qt::TextBrowserInteraction);
  m_summary->setOpenExternalLinks(true);
  layout->addWidget(m_summary);

  m_notes = new QTextBrowser(this);
  m_notes->setOpenExternalLinks(true);
  m_notes->setMinimumHeight(180);
  layout->addWidget(m_notes, 1);

  m_progress = new QProgressBar(this);
  m_progress->setVisible(false);
  layout->addWidget(m_progress);

  m_status = new QLabel(this);
  m_status->setWordWrap(true);
  m_status->setVisible(false);
  layout->addWidget(m_status);

  auto *buttons = new QDialogButtonBox(this);
  m_updateButton = buttons->addButton(tr("Update"), QDialogButtonBox::AcceptRole);
  m_skipButton = buttons->addButton(tr("Skip This Version"), QDialogButtonBox::DestructiveRole);
  m_laterButton = buttons->addButton(tr("Later"), QDialogButtonBox::RejectRole);
  m_restartButton = buttons->addButton(tr("Restart Now"), QDialogButtonBox::ApplyRole);
  m_restartButton->setVisible(false);
  layout->addWidget(buttons);

  connect(m_laterButton, &QPushButton::clicked, this, &QDialog::reject);
  connect(m_skipButton, &QPushButton::clicked, this, [this]() {
    emit skipRequested(m_info.latestVersion);
    reject();
  });
  connect(m_restartButton, &QPushButton::clicked, this, [this]() {
    emit restartRequested();
    accept();
  });

  if (!m_info.updateAvailable) {
    m_headline->setText(tr("VNote is up to date."));
    m_summary->setText(tr("You are running version %1.").arg(m_info.currentVersion));
    m_notes->setVisible(false);
    m_updateButton->setVisible(false);
    m_skipButton->setVisible(false);
    m_laterButton->setText(tr("Close"));
    resize(420, 160);
    return;
  }

  m_headline->setText(tr("VNote %1 is available.").arg(m_info.latestVersion));

  if (!m_info.eligible) {
    // In-place self-update is impossible here (MSI install, read-only
    // directory, unsupported filesystem, ...). Offer the download page instead
    // of pretending the button will work.
    m_summary->setText(tr("%1\n\nYou are running version %2.")
                           .arg(m_info.ineligibleReason, m_info.currentVersion));
    m_updateButton->setText(tr("Open Download Page"));
    connect(m_updateButton, &QPushButton::clicked, this, [this]() {
      QDesktopServices::openUrl(QUrl(m_info.releaseUrl));
      accept();
    });
  } else {
    m_summary->setText(tr("You are running version %1. The download is %2 (%3).")
                           .arg(m_info.currentVersion, formatBytes(m_info.downloadSize),
                                m_info.isDelta
                                    ? tr("incremental update, %n step(s)", nullptr,
                                         m_info.hopCount)
                                    : tr("full package")));
    connect(m_updateButton, &QPushButton::clicked, this, [this]() {
      emit downloadRequested();
      setDownloading();
    });
  }

  if (m_info.releaseNotes.isEmpty()) {
    m_notes->setVisible(false);
  } else {
    // Release bodies are Markdown; render them as plain text rather than
    // half-interpreting them as HTML.
    m_notes->setPlainText(m_info.releaseNotes);
  }

  resize(560, 460);
}

void UpdateDialog::setDownloading() {
  m_updateButton->setEnabled(false);
  m_skipButton->setEnabled(false);
  m_progress->setVisible(true);
  m_progress->setRange(0, 0);
  m_status->setVisible(true);
  m_status->setText(tr("Preparing the update..."));
}

void UpdateDialog::setProgress(const QString &p_stage, qint64 p_done, qint64 p_total) {
  m_progress->setVisible(true);
  if (p_total > 0) {
    // QProgressBar is int-based; scale to permille to stay well inside range
    // for multi-hundred-megabyte downloads.
    m_progress->setRange(0, 1000);
    m_progress->setValue(static_cast<int>((p_done * 1000) / p_total));
  } else {
    m_progress->setRange(0, 0);
  }

  m_status->setVisible(true);
  m_status->setText(p_stage);
}

void UpdateDialog::setReadyToApply(const QString &p_version) {
  m_progress->setRange(0, 1);
  m_progress->setValue(1);
  m_status->setVisible(true);
  m_status->setText(
      tr("VNote %1 has been downloaded and will be installed the next time VNote closes.")
          .arg(p_version));

  m_updateButton->setVisible(false);
  m_skipButton->setVisible(false);
  m_restartButton->setVisible(true);
  m_restartButton->setDefault(true);
  m_laterButton->setText(tr("Later"));
}

void UpdateDialog::setFailed(const QString &p_message) {
  m_progress->setVisible(false);
  m_status->setVisible(true);
  m_status->setText(tr("The update failed: %1").arg(p_message));

  m_updateButton->setEnabled(true);
  m_skipButton->setEnabled(true);
}
