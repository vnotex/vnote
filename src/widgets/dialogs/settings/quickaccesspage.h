#ifndef QUICKACCESSPAGE_H
#define QUICKACCESSPAGE_H

#include "settingspage.h"

#include <QHash>
#include <core/sessionconfig.h>

class QGroupBox;
class QPlainTextEdit;
class QComboBox;

namespace vnotex {
class LocationInputWithBrowseButton;
class LineEditWithSnippet;
class NoteTemplateSelector;

class QuickAccessPage : public SettingsPage {
  Q_OBJECT
public:
  explicit QuickAccessPage(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  QString title() const Q_DECL_OVERRIDE;

  QString slug() const Q_DECL_OVERRIDE;

protected:
  void loadInternal() Q_DECL_OVERRIDE;

  bool saveInternal() Q_DECL_OVERRIDE;

private:
  void setupUI();

  void newQuickNoteScheme();

  void removeQuickNoteScheme();

  void saveCurrentQuickNote();

  void loadCurrentQuickNote();

  void loadQuickNoteSchemes();

  void saveQuickNoteSchemes();

  void setCurrentQuickNote(int idx);

  void newQuickAccessItem();

  static QVector<SessionConfig::QuickAccessItem> parseQuickAccessText(const QString &p_text);
  static QString formatQuickAccessItems(const QVector<SessionConfig::QuickAccessItem> &p_items);

  static QString getDefaultQuickNoteFolderPath();

  QPlainTextEdit *m_quickAccessTextEdit = nullptr;

  QComboBox *m_quickNoteSchemeComboBox = nullptr;

  LocationInputWithBrowseButton *m_quickNoteFolderPathInput = nullptr;

  LineEditWithSnippet *m_quickNoteNoteNameLineEdit = nullptr;

  NoteTemplateSelector *m_quickNoteTemplateSelector = nullptr;

  QGroupBox *m_quickNoteInfoGroupBox = nullptr;

  QVector<SessionConfig::QuickNoteScheme> m_quickNoteSchemes;

  int m_quickNoteCurrentIndex = -1;

  // Hidden map: path -> UUID. Preserves UUIDs through the text-edit round-trip
  // so the user never sees UUIDs but they survive save/load.
  QHash<QString, QString> m_quickAccessUuidMap;
};
} // namespace vnotex

#endif // QUICKACCESSPAGE_H
