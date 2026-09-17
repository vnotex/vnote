#ifndef SEARCHPANEL2_H
#define SEARCHPANEL2_H

#include <QFrame>
#include <QStringList>

#include <core/nodeidentifier.h>
#include <core/noncopyable.h>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QProgressDialog;

namespace vnotex {

class SearchController;
class ServiceLocator;

class SearchPanel2 : public QFrame, private Noncopyable {
  Q_OBJECT

public:
  explicit SearchPanel2(ServiceLocator &p_services, QWidget *p_parent = nullptr);
  ~SearchPanel2() override;

  SearchController *getController() const;

public slots:
  void setCurrentNotebookId(const QString &p_notebookId);
  void setCurrentFolderId(const NodeIdentifier &p_folderId);

private:
  void setupUI();
  void restoreState();
  void setupConnections();
  void updateModeDependentOptions();

  void updateSearchInputs();
  void updateStatusLabel();
  void startSearch();
  void onSearchStarted();
  void onSearchFinished(int p_totalMatches, bool p_truncated, int p_encryptedSkippedCount);
  void onSearchFailed(const QString &p_errorMessage);
  void onSearchCancelled();
  void onProgressUpdated(int p_percent);
  void onReplacementConfirmationRequested(int p_matches, int p_files);
  void onReplacementStarted(int p_files);
  void onReplacementProgress(int p_completedFiles, int p_totalFiles);
  void onReplacementFinished(int p_replacedMatches, int p_savedFiles, bool p_cancelled,
                             const QStringList &p_errors);

  ServiceLocator &m_services;
  SearchController *m_controller = nullptr;

  QComboBox *m_keywordCombo = nullptr;
  QLabel *m_replacementLabel = nullptr;
  QLineEdit *m_replacementEdit = nullptr;
  QComboBox *m_scopeCombo = nullptr;
  QComboBox *m_modeCombo = nullptr;
  QCheckBox *m_caseSensitiveCheck = nullptr;
  QCheckBox *m_regexCheck = nullptr;
  QLineEdit *m_filePatternEdit = nullptr;
  QPushButton *m_searchButton = nullptr;
  QPushButton *m_replaceSelectedButton = nullptr;
  QPushButton *m_replaceAllButton = nullptr;
  QProgressDialog *m_replacementProgressDialog = nullptr;
  QProgressBar *m_progressBar = nullptr;
  QLabel *m_statusLabel = nullptr;

  QString m_searchStatus;
  QString m_replacementSummary;
  QString m_replacementStatus;

  bool m_initialized = false;
  bool m_searching = false;
  bool m_replacing = false;
  bool m_replacementCancelling = false;
  bool m_hasCurrentNotebook = false;
};

} // namespace vnotex

#endif // SEARCHPANEL2_H
