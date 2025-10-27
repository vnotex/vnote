#ifndef FILEASSOCIATIONPAGE_H
#define FILEASSOCIATIONPAGE_H

#include "settingspage.h"

class QGroupBox;

namespace vnotex {
class FileAssociationPage : public SettingsPage {
  Q_OBJECT
public:
  explicit FileAssociationPage(QWidget *p_parent = nullptr);

  QString title() const Q_DECL_OVERRIDE;

protected:
  void loadInternal() Q_DECL_OVERRIDE;

  bool saveInternal() Q_DECL_OVERRIDE;

private:
  void setupUI();

  void loadBuiltInTypesGroup(QGroupBox *p_box);

  void loadExternalProgramsGroup(QGroupBox *p_box);

  QGroupBox *m_builtInFileTypesBox = nullptr;

  QGroupBox *m_externalProgramsBox = nullptr;

  static const char *c_nameProperty;

  static const QChar c_suffixSeparator;
};
} // namespace vnotex

#endif // FILEASSOCIATIONPAGE_H
