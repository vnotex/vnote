#ifndef FILEASSOCIATIONPAGE_H
#define FILEASSOCIATIONPAGE_H

#include "settingspage.h"

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace vnotex {
class FileAssociationPage : public SettingsPage {
  Q_OBJECT
public:
  explicit FileAssociationPage(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  QString title() const Q_DECL_OVERRIDE;

  QString slug() const Q_DECL_OVERRIDE;

protected:
  void loadInternal() Q_DECL_OVERRIDE;

  bool saveInternal() Q_DECL_OVERRIDE;

private:
  void setupUI();

  void loadBuiltInTypesGroup();

  void loadExternalProgramsGroup();

  void addExternalProgramRow(const QString &p_name, const QString &p_command,
                             const QString &p_suffixes);

  void removeExternalProgramRow(QWidget *p_row);

  void addSystemProgramRow(const QString &p_suffixes);

  QVBoxLayout *m_builtInCardLayout = nullptr;
  int m_builtInRowStartIndex = 0;

  QVBoxLayout *m_externalCardLayout = nullptr;
  int m_externalRowStartIndex = 0;

  QPushButton *m_addProgramButton = nullptr;

  static const char *c_nameProperty;

  static const QChar c_suffixSeparator;
};
} // namespace vnotex

#endif // FILEASSOCIATIONPAGE_H
