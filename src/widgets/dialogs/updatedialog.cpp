#include "updatedialog.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

using namespace vnotex;

UpdateDialog::UpdateDialog(const UpdateInfo &p_info, QWidget *p_parent)
    : QDialog(p_parent), m_info(p_info) {
  setupUi();
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

  m_status = new QLabel(this);
  m_status->setWordWrap(true);
  m_status->setVisible(false);
  layout->addWidget(m_status);

  auto *buttons = new QDialogButtonBox(this);
  m_releasePageButton = buttons->addButton(tr("Open Release Page"), QDialogButtonBox::AcceptRole);
  m_skipButton = buttons->addButton(tr("Skip This Version"), QDialogButtonBox::DestructiveRole);
  m_laterButton = buttons->addButton(tr("Later"), QDialogButtonBox::RejectRole);
  layout->addWidget(buttons);

  connect(m_laterButton, &QPushButton::clicked, this, &QDialog::reject);
  connect(m_skipButton, &QPushButton::clicked, this, [this]() {
    emit skipRequested(m_info.latestVersion);
    reject();
  });

  if (!m_info.updateAvailable) {
    m_headline->setText(tr("VNote is up to date."));
    m_summary->setText(tr("You are running version %1.").arg(m_info.currentVersion));
    m_notes->setVisible(false);
    m_releasePageButton->setVisible(false);
    m_skipButton->setVisible(false);
    m_laterButton->setText(tr("Close"));
    resize(420, 160);
    return;
  }

  m_headline->setText(tr("VNote %1 is available.").arg(m_info.latestVersion));
  m_summary->setText(tr("You are running version %1. Open the release page to download the new "
                        "version and install it yourself.")
                         .arg(m_info.currentVersion));

  connect(m_releasePageButton, &QPushButton::clicked, this, [this]() {
    QDesktopServices::openUrl(QUrl(m_info.releaseUrl));
    accept();
  });

  if (m_info.releaseNotes.isEmpty()) {
    m_notes->setVisible(false);
  } else {
    // Release bodies are Markdown; render them as plain text rather than
    // half-interpreting them as HTML.
    m_notes->setPlainText(m_info.releaseNotes);
  }

  resize(560, 460);
}

void UpdateDialog::setFailed(const QString &p_message) {
  m_status->setVisible(true);
  m_status->setText(tr("Could not check for updates: %1").arg(p_message));
}
