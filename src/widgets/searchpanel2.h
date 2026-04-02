#ifndef SEARCHPANEL2_H
#define SEARCHPANEL2_H

#include <QFrame>

#include <core/nodeidentifier.h>
#include <core/noncopyable.h>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

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
  void setupConnections();

  void startSearch();
  void onSearchStarted();
  void onSearchFinished(int p_totalMatches, bool p_truncated);
  void onSearchFailed(const QString &p_errorMessage);
  void onSearchCancelled();
  void onProgressUpdated(int p_percent);

  ServiceLocator &m_services;
  SearchController *m_controller = nullptr;

  QLineEdit *m_keywordEdit = nullptr;
  QComboBox *m_scopeCombo = nullptr;
  QComboBox *m_modeCombo = nullptr;
  QCheckBox *m_caseSensitiveCheck = nullptr;
  QCheckBox *m_regexCheck = nullptr;
  QLineEdit *m_filePatternEdit = nullptr;
  QPushButton *m_searchButton = nullptr;
  QProgressBar *m_progressBar = nullptr;
  QLabel *m_statusLabel = nullptr;

  bool m_searching = false;
};

} // namespace vnotex

#endif // SEARCHPANEL2_H
