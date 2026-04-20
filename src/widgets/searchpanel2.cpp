#include "searchpanel2.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

#include <controllers/searchcontroller.h>
#include <core/configmgr2.h>
#include <core/sessionconfig.h>
#include <core/servicelocator.h>
#include <core/widgetconfig.h>
#include <widgets/widgetsfactory.h>

using namespace vnotex;

SearchPanel2::SearchPanel2(ServiceLocator &p_services, QWidget *p_parent)
    : QFrame(p_parent), m_services(p_services) {
  m_controller = new SearchController(m_services, this);
  setupUI();
  restoreState();
  setupConnections();
  m_initialized = true;
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

  m_keywordCombo = WidgetsFactory::createEditableComboBox(this);
  m_keywordCombo->setInsertPolicy(QComboBox::NoInsert);
  m_keywordCombo->lineEdit()->setPlaceholderText(tr("Search..."));
  m_keywordCombo->lineEdit()->setClearButtonEnabled(true);
  m_keywordCombo->setMaxVisibleItems(10);
  m_keywordCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  mainLayout->addWidget(m_keywordCombo);

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

  setFocusProxy(m_keywordCombo);

  updateModeDependentOptions();
}

void SearchPanel2::setupConnections() {
  connect(m_keywordCombo->lineEdit(), &QLineEdit::returnPressed, this, &SearchPanel2::startSearch);
  connect(m_filePatternEdit, &QLineEdit::returnPressed, this, &SearchPanel2::startSearch);
  connect(m_scopeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int idx) {
            if (!m_initialized) {
              return;
            }

            auto *cm = m_services.get<ConfigMgr2>();
            if (cm) {
              cm->getWidgetConfig().setSearchScope(idx);
            }
          });
  connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int) { updateModeDependentOptions(); });
  connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int idx) {
            if (!m_initialized) {
              return;
            }

            auto *cm = m_services.get<ConfigMgr2>();
            if (cm) {
              cm->getWidgetConfig().setSearchMode(idx);
            }
          });
  connect(m_caseSensitiveCheck, &QCheckBox::toggled, this, [this](bool checked) {
    if (!m_initialized) {
      return;
    }

    auto *cm = m_services.get<ConfigMgr2>();
    if (cm) {
      cm->getWidgetConfig().setSearchCaseSensitive(checked);
    }
  });
  connect(m_regexCheck, &QCheckBox::toggled, this, [this](bool checked) {
    if (!m_initialized) {
      return;
    }

    auto *cm = m_services.get<ConfigMgr2>();
    if (cm) {
      cm->getWidgetConfig().setSearchRegex(checked);
    }
  });
  connect(m_filePatternEdit, &QLineEdit::editingFinished, this, [this]() {
    if (!m_initialized) {
      return;
    }

    auto *cm = m_services.get<ConfigMgr2>();
    if (cm) {
      cm->getWidgetConfig().setSearchFilePattern(m_filePatternEdit->text().trimmed());
    }
  });

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

  updateModeDependentOptions();
}

void SearchPanel2::restoreState() {
  auto *configMgr = m_services.get<ConfigMgr2>();
  if (!configMgr) {
    return;
  }

  auto &wc = configMgr->getWidgetConfig();
  m_scopeCombo->setCurrentIndex(wc.getSearchScope());
  m_modeCombo->setCurrentIndex(wc.getSearchMode());
  m_caseSensitiveCheck->setChecked(wc.getSearchCaseSensitive());
  m_regexCheck->setChecked(wc.getSearchRegex());
  m_filePatternEdit->setText(wc.getSearchFilePattern());

  auto &sc = configMgr->getSessionConfig();
  const auto &history = sc.getSearchHistory();
  m_keywordCombo->addItems(history);
  m_keywordCombo->setCurrentText(QString());

  updateModeDependentOptions();
}

void SearchPanel2::updateModeDependentOptions() {
  const bool content_mode = m_modeCombo->currentIndex() == SearchController::ContentSearch;
  m_caseSensitiveCheck->setEnabled(content_mode);
  m_regexCheck->setEnabled(content_mode);
}

void SearchPanel2::startSearch() {
  const QString keyword = m_keywordCombo->currentText().trimmed();
  if (keyword.isEmpty()) {
    return;
  }

  auto *configMgr = m_services.get<ConfigMgr2>();
  if (configMgr) {
    configMgr->getSessionConfig().addSearchHistory(keyword);
    m_keywordCombo->clear();
    m_keywordCombo->addItems(configMgr->getSessionConfig().getSearchHistory());
    m_keywordCombo->setCurrentText(keyword);
  }

  const int scope = m_scopeCombo->currentIndex();
  const int mode = m_modeCombo->currentIndex();
  const bool caseSensitive = m_caseSensitiveCheck->isChecked();
  const bool useRegex = m_regexCheck->isChecked();
  const QString filePattern = m_filePatternEdit->text().trimmed();

  qDebug() << "SearchPanel2::startSearch: keyword:" << keyword
           << "scope:" << scope << "mode:" << mode
           << "caseSensitive:" << caseSensitive << "regex:" << useRegex
           << "filePattern:" << filePattern;

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
  qDebug() << "SearchPanel2::onSearchFinished: totalMatches:" << p_totalMatches
           << "truncated:" << p_truncated;

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
  qWarning() << "SearchPanel2::onSearchFailed:" << p_errorMessage;
  m_searching = false;
  m_searchButton->setText(tr("Search"));
  m_progressBar->hide();
  m_statusLabel->setText(tr("Error: %1").arg(p_errorMessage));
}

void SearchPanel2::onSearchCancelled() {
  qDebug() << "SearchPanel2::onSearchCancelled";
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
