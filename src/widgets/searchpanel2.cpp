#include "searchpanel2.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

#include <controllers/searchcontroller.h>
#include <core/servicelocator.h>

using namespace vnotex;

SearchPanel2::SearchPanel2(ServiceLocator &p_services, QWidget *p_parent)
    : QFrame(p_parent), m_services(p_services) {
  m_controller = new SearchController(m_services, this);
  setupUI();
  setupConnections();
}

SearchPanel2::~SearchPanel2() = default;

SearchController *SearchPanel2::getController() const {
  return m_controller;
}

void SearchPanel2::setCurrentNotebookId(const QString &p_notebookId) {
  m_controller->setCurrentNotebookId(p_notebookId);
  if (m_searchButton) {
    m_searchButton->setEnabled(!p_notebookId.isEmpty());
  }
}

void SearchPanel2::setCurrentFolderId(const NodeIdentifier &p_folderId) {
  m_controller->setCurrentFolderId(p_folderId);
}

void SearchPanel2::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(8, 8, 8, 8);
  mainLayout->setSpacing(6);

  auto *keywordLabel = new QLabel(tr("Keyword:"), this);
  mainLayout->addWidget(keywordLabel);

  m_keywordEdit = new QLineEdit(this);
  m_keywordEdit->setPlaceholderText(tr("Search..."));
  m_keywordEdit->setClearButtonEnabled(true);
  mainLayout->addWidget(m_keywordEdit);

  auto *modeLabel = new QLabel(tr("Mode:"), this);
  mainLayout->addWidget(modeLabel);

  m_modeCombo = new QComboBox(this);
  m_modeCombo->addItem(tr("File Name"));
  m_modeCombo->addItem(tr("Content"));
  m_modeCombo->addItem(tr("Tag"));
  mainLayout->addWidget(m_modeCombo);

  auto *scopeLabel = new QLabel(tr("Scope:"), this);
  mainLayout->addWidget(scopeLabel);

  m_scopeCombo = new QComboBox(this);
  m_scopeCombo->addItem(tr("Buffers"));
  m_scopeCombo->addItem(tr("Current Folder"));
  m_scopeCombo->addItem(tr("Current Notebook"));
  m_scopeCombo->addItem(tr("All Notebooks"));
  mainLayout->addWidget(m_scopeCombo);

  auto *optionsLabel = new QLabel(tr("Options:"), this);
  mainLayout->addWidget(optionsLabel);

  auto *optionsLayout = new QHBoxLayout();
  optionsLayout->setContentsMargins(0, 0, 0, 0);
  optionsLayout->setSpacing(8);

  m_caseSensitiveCheck = new QCheckBox(tr("Case Sensitive"), this);
  optionsLayout->addWidget(m_caseSensitiveCheck);

  m_regexCheck = new QCheckBox(tr("Regex"), this);
  optionsLayout->addWidget(m_regexCheck);

  optionsLayout->addStretch();
  mainLayout->addLayout(optionsLayout);

  auto *patternLabel = new QLabel(tr("File Pattern:"), this);
  mainLayout->addWidget(patternLabel);

  m_filePatternEdit = new QLineEdit(this);
  m_filePatternEdit->setPlaceholderText(tr("File pattern (e.g., *.md)"));
  m_filePatternEdit->setClearButtonEnabled(true);
  mainLayout->addWidget(m_filePatternEdit);

  m_searchButton = new QPushButton(tr("Search"), this);
  m_searchButton->setEnabled(false);
  mainLayout->addWidget(m_searchButton);

  m_progressBar = new QProgressBar(this);
  m_progressBar->setRange(0, 0);
  m_progressBar->hide();
  mainLayout->addWidget(m_progressBar);

  m_statusLabel = new QLabel(this);
  mainLayout->addWidget(m_statusLabel);

  mainLayout->addStretch();

  setFocusProxy(m_keywordEdit);
}

void SearchPanel2::setupConnections() {
  connect(m_keywordEdit, &QLineEdit::returnPressed, this, &SearchPanel2::startSearch);

  connect(m_searchButton, &QPushButton::clicked, this, [this]() {
    if (m_searching) {
      m_controller->cancel();
    } else {
      startSearch();
    }
  });

  connect(m_controller, &SearchController::searchStarted,
          this, &SearchPanel2::onSearchStarted);
  connect(m_controller, &SearchController::searchFinished,
          this, &SearchPanel2::onSearchFinished);
  connect(m_controller, &SearchController::searchFailed,
          this, &SearchPanel2::onSearchFailed);
  connect(m_controller, &SearchController::searchCancelled,
          this, &SearchPanel2::onSearchCancelled);
  connect(m_controller, &SearchController::progressUpdated,
          this, &SearchPanel2::onProgressUpdated);
}

void SearchPanel2::startSearch() {
  const QString keyword = m_keywordEdit->text().trimmed();
  if (keyword.isEmpty()) {
    return;
  }

  const int scope = m_scopeCombo->currentIndex();
  const int mode = m_modeCombo->currentIndex();
  const bool caseSensitive = m_caseSensitiveCheck->isChecked();
  const bool useRegex = m_regexCheck->isChecked();
  const QString filePattern = m_filePatternEdit->text().trimmed();

  m_controller->search(keyword, scope, mode, caseSensitive, useRegex, filePattern);
}

void SearchPanel2::onSearchStarted() {
  m_searching = true;
  m_searchButton->setText(tr("Cancel"));
  m_progressBar->setRange(0, 0);
  m_progressBar->show();
  m_statusLabel->setText(tr("Searching..."));
}

void SearchPanel2::onSearchFinished(int p_totalMatches, bool p_truncated) {
  m_searching = false;
  m_searchButton->setText(tr("Search"));
  m_progressBar->hide();

  QString status = tr("%n result(s)", "", p_totalMatches);
  if (p_truncated) {
    status += tr(" (truncated)");
  }
  m_statusLabel->setText(status);
}

void SearchPanel2::onSearchFailed(const QString &p_errorMessage) {
  m_searching = false;
  m_searchButton->setText(tr("Search"));
  m_progressBar->hide();
  m_statusLabel->setText(tr("Error: %1").arg(p_errorMessage));
}

void SearchPanel2::onSearchCancelled() {
  m_searching = false;
  m_searchButton->setText(tr("Search"));
  m_progressBar->hide();
  m_statusLabel->setText(tr("Search cancelled"));
}

void SearchPanel2::onProgressUpdated(int p_percent) {
  if (p_percent > 0) {
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(p_percent);
  }
}
