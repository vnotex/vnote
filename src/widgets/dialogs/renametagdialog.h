#ifndef RENAMETAGDIALOG_H
#define RENAMETAGDIALOG_H

#include "scrolldialog.h"

namespace vnotex {
class TagI;
class LineEditWithSnippet;

class RenameTagDialog : public ScrollDialog {
  Q_OBJECT
public:
  RenameTagDialog(TagI *p_tagI, const QString &p_name, QWidget *p_parent = nullptr);

  QString getTagName() const;

protected:
  void acceptedButtonClicked() Q_DECL_OVERRIDE;

private:
  void setupUI();

  bool validateInputs();

  bool renameTag();

  TagI *m_tagI = nullptr;

  const QString m_tagName;

  LineEditWithSnippet *m_nameLineEdit = nullptr;
};
} // namespace vnotex

#endif // RENAMETAGDIALOG_H
