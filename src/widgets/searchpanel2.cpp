#include "searchpanel2.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDebug>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <controllers/searchcontroller.h>
#include <core/configmgr2.h>
#include <core/servicelocator.h>
#include <core/sessionconfig.h>
#include <core/widgetconfig.h>
#include <widgets/widgetsfactory.h>

using namespace vnotex;

namespace {
class ReplacementProgressDialog final : public QProgressDialog {
public:
  explicit ReplacementProgressDialog(QWidget *p_parent) : QProgressDialog(p_parent) {
    // Cancellation requests stop the batch, not the dialog: an active write must settle first.
    disconnect(this, SIGNAL(canceled()), this, SLOT(cancel()));
  }

  void reject() override { emit canceled(); }

protected:
  void closeEvent(QCloseEvent *p_event) override {
    p_event->ignore();
    emit canceled();
  }
};
} // namespace

SearchPanel2::SearchPanel2(ServiceLocator &p_services, QWidget *p_parent)
    : QFrame(p_parent), m_services(p_services) {
  m_controller = new SearchController(m_services, this);
  setupUI();
  restoreState();
  setupConnections();
  m_initialized = true;
}

SearchPanel2::~SearchPanel2() = default;

SearchController *SearchPanel2::getController() const { return m_controller; }

void SearchPanel2::setCurrentNotebookId(const QString &p_notebookId) {
  m_controller->setCurrentNotebookId(p_notebookId);
  m_hasCurrentNotebook = !p_notebookId.isEmpty();
  updateSearchInputs();
}

void SearchPanel2::setCurrentFolderId(const NodeIdentifier &p_folderId) {
  m_controller->setCurrentFolderId(p_folderId);
}

void SearchPanel2::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(8, 8, 8, 8);
  mainLayout->setSpacing(6);

  auto *keywordLabel = new QLabel(tr("Keyword"), this);
  mainLayout->addWidget(keywordLabel);

  m_keywordCombo = WidgetsFactory::createEditableComboBox(this);
  m_keywordCombo->setObjectName(QStringLiteral("searchKeywordCombo"));
  m_keywordCombo->setInsertPolicy(QComboBox::NoInsert);
  m_keywordCombo->lineEdit()->setPlaceholderText(tr("Search..."));
  m_keywordCombo->lineEdit()->setClearButtonEnabled(true);
  m_keywordCombo->setMaxVisibleItems(10);
  m_keywordCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  m_keywordCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_keywordCombo->setMinimumContentsLength(10);
  mainLayout->addWidget(m_keywordCombo);

  m_replacementLabel = new QLabel(tr("Replace with"), this);
  mainLayout->addWidget(m_replacementLabel);

  m_replacementEdit = new QLineEdit(this);
  m_replacementEdit->setObjectName(QStringLiteral("replacementEdit"));
  m_replacementEdit->setAccessibleName(tr("Replace with"));
  m_replacementEdit->setClearButtonEnabled(true);
  m_replacementEdit->setToolTip(tr("Replacement text is literal, including in Regex mode"));
  m_replacementLabel->setBuddy(m_replacementEdit);
  mainLayout->addWidget(m_replacementEdit);

  auto *modeLabel = new QLabel(tr("Mode"), this);
  mainLayout->addWidget(modeLabel);

  m_modeCombo = new QComboBox(this);
  m_modeCombo->setObjectName(QStringLiteral("searchModeCombo"));
  m_modeCombo->addItem(tr("File Name"));
  m_modeCombo->addItem(tr("Content"));
  m_modeCombo->addItem(tr("Tag"));
  mainLayout->addWidget(m_modeCombo);

  auto *scopeLabel = new QLabel(tr("Scope"), this);
  mainLayout->addWidget(scopeLabel);

  m_scopeCombo = new QComboBox(this);
  m_scopeCombo->setObjectName(QStringLiteral("searchScopeCombo"));
  m_scopeCombo->addItem(tr("Buffers"));
  m_scopeCombo->addItem(tr("Current Folder"));
  m_scopeCombo->addItem(tr("Current Notebook"));
  m_scopeCombo->addItem(tr("All Notebooks"));
  mainLayout->addWidget(m_scopeCombo);

  auto *optionsLabel = new QLabel(tr("Options"), this);
  mainLayout->addWidget(optionsLabel);

  auto *optionsLayout = new QHBoxLayout();
  optionsLayout->setContentsMargins(0, 0, 0, 0);
  optionsLayout->setSpacing(8);

  m_caseSensitiveCheck = new QCheckBox(tr("Case Sensitive"), this);
  m_caseSensitiveCheck->setObjectName(QStringLiteral("searchCaseSensitiveCheck"));
  optionsLayout->addWidget(m_caseSensitiveCheck);

  m_regexCheck = new QCheckBox(tr("Regex"), this);
  m_regexCheck->setObjectName(QStringLiteral("searchRegexCheck"));
  optionsLayout->addWidget(m_regexCheck);

  optionsLayout->addStretch();
  mainLayout->addLayout(optionsLayout);

  auto *patternLabel = new QLabel(tr("File pattern"), this);
  mainLayout->addWidget(patternLabel);

  m_filePatternEdit = new QLineEdit(this);
  m_filePatternEdit->setObjectName(QStringLiteral("searchFilePatternEdit"));
  m_filePatternEdit->setPlaceholderText(tr("File pattern (e.g., *.md)"));
  m_filePatternEdit->setClearButtonEnabled(true);
  mainLayout->addWidget(m_filePatternEdit);

  m_searchButton = new QPushButton(tr("Search"), this);
  m_searchButton->setObjectName(QStringLiteral("searchButton"));
  m_searchButton->setEnabled(false);
  mainLayout->addWidget(m_searchButton);

  m_replaceSelectedButton = new QPushButton(tr("Replace Selected"), this);
  m_replaceSelectedButton->setObjectName(QStringLiteral("replaceSelectedButton"));
  m_replaceSelectedButton->setAutoDefault(false);
  m_replaceSelectedButton->setEnabled(false);
  mainLayout->addWidget(m_replaceSelectedButton);

  m_replaceAllButton = new QPushButton(tr("Replace All"), this);
  m_replaceAllButton->setObjectName(QStringLiteral("replaceAllButton"));
  m_replaceAllButton->setAutoDefault(false);
  m_replaceAllButton->setEnabled(false);
  mainLayout->addWidget(m_replaceAllButton);

  m_progressBar = new QProgressBar(this);
  m_progressBar->setRange(0, 0);
  m_progressBar->hide();
  mainLayout->addWidget(m_progressBar);

  m_statusLabel = new QLabel(this);
  m_statusLabel->setObjectName(QStringLiteral("searchStatusLabel"));
  m_statusLabel->setTextFormat(Qt::PlainText);
  m_statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  m_statusLabel->setWordWrap(true);
  mainLayout->addWidget(m_statusLabel);

  mainLayout->addStretch();

  setFocusProxy(m_keywordCombo);

  updateModeDependentOptions();
}

void SearchPanel2::setupConnections() {
  connect(m_keywordCombo->lineEdit(), &QLineEdit::returnPressed, this, &SearchPanel2::startSearch);
  connect(m_filePatternEdit, &QLineEdit::returnPressed, this, &SearchPanel2::startSearch);
  connect(m_keywordCombo, &QComboBox::currentTextChanged, m_controller,
          &SearchController::invalidateReplacementResults);
  connect(m_scopeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), m_controller,
          &SearchController::invalidateReplacementResults);
  connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), m_controller,
          &SearchController::invalidateReplacementResults);
  connect(m_caseSensitiveCheck, &QCheckBox::toggled, m_controller,
          &SearchController::invalidateReplacementResults);
  connect(m_regexCheck, &QCheckBox::toggled, m_controller,
          &SearchController::invalidateReplacementResults);
  connect(m_filePatternEdit, &QLineEdit::textChanged, m_controller,
          &SearchController::invalidateReplacementResults);
  connect(m_scopeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
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
  connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
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
    if (m_replacing) {
      return;
    }
    if (m_searching) {
      m_controller->cancel();
    } else {
      startSearch();
    }
  });
  connect(m_replaceSelectedButton, &QPushButton::clicked, this,
          [this]() { m_controller->requestReplacement(m_replacementEdit->text(), false); });
  connect(m_replaceAllButton, &QPushButton::clicked, this,
          [this]() { m_controller->requestReplacement(m_replacementEdit->text(), true); });

  connect(m_controller, &SearchController::searchStarted, this, &SearchPanel2::onSearchStarted);
  connect(m_controller, &SearchController::searchFinished, this, &SearchPanel2::onSearchFinished);
  connect(m_controller, &SearchController::searchFailed, this, &SearchPanel2::onSearchFailed);
  connect(m_controller, &SearchController::searchCancelled, this, &SearchPanel2::onSearchCancelled);
  connect(m_controller, &SearchController::progressUpdated, this, &SearchPanel2::onProgressUpdated);
  connect(m_controller, &SearchController::replacementAvailabilityChanged, this,
          [this](bool p_selected, bool p_all) {
            m_replaceSelectedButton->setEnabled(p_selected);
            m_replaceAllButton->setEnabled(p_all);
          });
  connect(m_controller, &SearchController::replacementStatusChanged, this,
          [this](const QString &p_reason) {
            m_replacementStatus = p_reason;
            updateStatusLabel();
          });
  connect(m_controller, &SearchController::replacementConfirmationRequested, this,
          &SearchPanel2::onReplacementConfirmationRequested);
  connect(m_controller, &SearchController::replacementStarted, this,
          &SearchPanel2::onReplacementStarted);
  connect(m_controller, &SearchController::replacementProgress, this,
          &SearchPanel2::onReplacementProgress);
  connect(m_controller, &SearchController::replacementFinished, this,
          &SearchPanel2::onReplacementFinished);

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
  m_caseSensitiveCheck->setEnabled(content_mode && !m_replacing);
  m_regexCheck->setEnabled(content_mode && !m_replacing);
  m_replacementLabel->setVisible(content_mode);
  m_replacementEdit->setVisible(content_mode);
  m_replaceSelectedButton->setVisible(content_mode);
  m_replaceAllButton->setVisible(content_mode);
}

void SearchPanel2::updateSearchInputs() {
  m_keywordCombo->setEnabled(!m_replacing);
  m_scopeCombo->setEnabled(!m_replacing);
  m_modeCombo->setEnabled(!m_replacing);
  m_filePatternEdit->setEnabled(!m_replacing);
  m_replacementEdit->setEnabled(!m_replacing);
  m_searchButton->setEnabled(!m_replacing && (m_hasCurrentNotebook || m_searching));
  updateModeDependentOptions();
}

void SearchPanel2::updateStatusLabel() {
  QStringList status;
  if (!m_replacementSummary.isEmpty()) {
    status.append(m_replacementSummary);
  }
  if (!m_searchStatus.isEmpty()) {
    status.append(m_searchStatus);
  }
  if (!m_replacementStatus.isEmpty()) {
    status.append(m_replacementStatus);
  }
  m_statusLabel->setText(status.join(QLatin1Char('\n')));
}

void SearchPanel2::startSearch() {
  if (m_replacing) {
    return;
  }
  const int mode = m_modeCombo->currentIndex();
  const QString keyword = mode == SearchController::ContentSearch
                              ? m_keywordCombo->currentText()
                              : m_keywordCombo->currentText().trimmed();
  if (keyword.isEmpty()) {
    return;
  }

  // Only an explicitly submitted search clears the last replacement outcome.
  m_replacementSummary.clear();
  m_searchStatus.clear();
  m_replacementStatus.clear();
  updateStatusLabel();

  auto *configMgr = m_services.get<ConfigMgr2>();
  if (configMgr) {
    configMgr->getSessionConfig().addSearchHistory(keyword);
    m_keywordCombo->clear();
    m_keywordCombo->addItems(configMgr->getSessionConfig().getSearchHistory());
    m_keywordCombo->setCurrentText(keyword);
  }

  const int scope = m_scopeCombo->currentIndex();
  const bool caseSensitive = m_caseSensitiveCheck->isChecked();
  const bool useRegex = m_regexCheck->isChecked();
  const QString filePattern = m_filePatternEdit->text().trimmed();

  qDebug() << "SearchPanel2::startSearch: keyword:" << keyword << "scope:" << scope
           << "mode:" << mode << "caseSensitive:" << caseSensitive << "regex:" << useRegex
           << "filePattern:" << filePattern;

  SearchController::FileSearchOptions fileSearchOptions;
  m_controller->search(keyword, scope, mode, caseSensitive, useRegex, filePattern,
                       fileSearchOptions);
}

void SearchPanel2::onSearchStarted() {
  m_searching = true;
  m_searchButton->setText(tr("Cancel"));
  m_progressBar->setRange(0, 0);
  m_progressBar->show();
  m_searchStatus =
      m_replacementSummary.isEmpty() ? tr("Searching...") : tr("Refreshing search results...");
  updateSearchInputs();
  updateStatusLabel();
}

void SearchPanel2::onSearchFinished(int p_totalMatches, bool p_truncated,
                                    int p_encryptedSkippedCount) {
  qDebug() << "SearchPanel2::onSearchFinished: totalMatches:" << p_totalMatches
           << "truncated:" << p_truncated;

  m_searching = false;
  m_searchButton->setText(tr("Search"));
  m_progressBar->hide();

  QString status = tr("%n result(s)", "", p_totalMatches);
  if (p_truncated) {
    status += tr(" (truncated)");
  }
  if (p_encryptedSkippedCount > 0) {
    status +=
        tr("; %n protected note(s) excluded from content search", "", p_encryptedSkippedCount);
  }
  m_searchStatus = status;
  updateSearchInputs();
  updateStatusLabel();
}

void SearchPanel2::onSearchFailed(const QString &p_errorMessage) {
  qWarning() << "SearchPanel2::onSearchFailed:" << p_errorMessage;
  m_searching = false;
  m_searchButton->setText(tr("Search"));
  m_progressBar->hide();
  m_searchStatus = tr("Error: %1").arg(p_errorMessage);
  updateSearchInputs();
  updateStatusLabel();
}

void SearchPanel2::onSearchCancelled() {
  qDebug() << "SearchPanel2::onSearchCancelled";
  m_searching = false;
  m_searchButton->setText(tr("Search"));
  m_progressBar->hide();
  m_searchStatus = tr("Search cancelled");
  updateSearchInputs();
  updateStatusLabel();
}

void SearchPanel2::onProgressUpdated(int p_percent) {
  if (p_percent > 0) {
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(p_percent);
  }
}

void SearchPanel2::onReplacementConfirmationRequested(int p_matches, int p_files) {
  auto *confirmation =
      new QMessageBox(QMessageBox::Question, tr("Replace Text"),
                      tr("Replace %1 match(es) in %2 file(s)?").arg(p_matches).arg(p_files),
                      QMessageBox::Ok | QMessageBox::Cancel, this);
  confirmation->setAttribute(Qt::WA_DeleteOnClose);
  confirmation->setTextFormat(Qt::PlainText);
  confirmation->setInformativeText(
      tr("Affected notes will be saved, including existing unsaved edits. "
         "This operation cannot be undone across files."));
  confirmation->button(QMessageBox::Ok)->setText(tr("Replace"));
  confirmation->setDefaultButton(QMessageBox::Cancel);
  confirmation->setEscapeButton(QMessageBox::Cancel);
  connect(confirmation, &QDialog::finished, this,
          [this](int p_result) { m_controller->confirmReplacement(p_result == QMessageBox::Ok); });
  confirmation->open();
}

void SearchPanel2::onReplacementStarted(int p_files) {
  m_replacing = true;
  m_replacementCancelling = false;
  updateSearchInputs();
  m_searchStatus = tr("Replacing text...");
  updateStatusLabel();

  m_replacementProgressDialog = new ReplacementProgressDialog(this);
  m_replacementProgressDialog->setObjectName(QStringLiteral("replacementProgressDialog"));
  m_replacementProgressDialog->setWindowTitle(tr("Replacing Text"));
  m_replacementProgressDialog->setWindowModality(Qt::ApplicationModal);
  m_replacementProgressDialog->setCancelButtonText(tr("Cancel"));
  m_replacementProgressDialog->setAutoClose(false);
  m_replacementProgressDialog->setAutoReset(false);
  m_replacementProgressDialog->setMinimumDuration(0);
  m_replacementProgressDialog->setRange(0, p_files);
  m_replacementProgressDialog->setLabelText(tr("Completed %1 of %2 file(s)").arg(0).arg(p_files));
  connect(m_replacementProgressDialog, &QProgressDialog::canceled, this, [this]() {
    if (!m_replacing || m_replacementCancelling) {
      return;
    }
    m_replacementCancelling = true;
    m_replacementProgressDialog->setCancelButton(nullptr);
    m_replacementProgressDialog->setLabelText(
        tr("Cancelling... Waiting for the current note to finish saving."));
    m_controller->cancel();
  });
  m_replacementProgressDialog->setValue(0);
  m_replacementProgressDialog->show();
}

void SearchPanel2::onReplacementProgress(int p_completedFiles, int p_totalFiles) {
  if (!m_replacementProgressDialog) {
    return;
  }
  m_replacementProgressDialog->setRange(0, p_totalFiles);
  if (!m_replacementCancelling) {
    m_replacementProgressDialog->setLabelText(
        tr("Completed %1 of %2 file(s)").arg(p_completedFiles).arg(p_totalFiles));
  }
  m_replacementProgressDialog->setValue(p_completedFiles);
}

void SearchPanel2::onReplacementFinished(int p_replacedMatches, int p_savedFiles, bool p_cancelled,
                                         const QStringList &p_errors) {
  if (m_replacementProgressDialog) {
    m_replacementProgressDialog->hide();
    m_replacementProgressDialog->deleteLater();
    m_replacementProgressDialog = nullptr;
  }
  m_replacing = false;
  m_replacementCancelling = false;
  updateSearchInputs();
  m_replacementSummary =
      tr("Replaced and saved %1 match(es) in %2 file(s).").arg(p_replacedMatches).arg(p_savedFiles);
  if (p_cancelled) {
    m_replacementSummary += QLatin1Char('\n');
    m_replacementSummary += p_savedFiles > 0
                                ? tr("Cancelled after partial completion; saved changes were kept.")
                                : tr("Replacement cancelled. No replacement changes were saved.");
  }
  if (!p_errors.isEmpty()) {
    m_replacementSummary += QLatin1Char('\n') + tr("Some notes could not be replaced or saved.");
  }
  m_searchStatus.clear();
  updateStatusLabel();

  if (!p_errors.isEmpty()) {
    auto *errors =
        new QMessageBox(QMessageBox::Warning, tr("Replace Text"),
                        tr("Some notes could not be replaced or saved."), QMessageBox::Ok, this);
    errors->setAttribute(Qt::WA_DeleteOnClose);
    errors->setTextFormat(Qt::PlainText);
    errors->setInformativeText(tr("Replacement edits in notes opened for recovery remain unsaved. "
                                  "Review the details and save those notes manually."));
    errors->setDetailedText(p_errors.join(QLatin1Char('\n')));
    errors->open();
  }
}
