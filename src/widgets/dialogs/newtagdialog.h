#ifndef NEWTAGDIALOG_H
#define NEWTAGDIALOG_H

#include "scrolldialog.h"

namespace vnotex {
class TagI;
class Tag;
class LevelLabelWithUpButton;
class LineEditWithSnippet;

class NewTagDialog : public ScrollDialog {
  Q_OBJECT
public:
  // New a tag under @p_tag.
  NewTagDialog(TagI *p_tagI, Tag *p_tag, QWidget *p_parent = nullptr);

protected:
  void acceptedButtonClicked() Q_DECL_OVERRIDE;

private:
  void setupUI();

  bool validateInputs();

  bool newTag();

  QString getTagName() const;

  TagI *m_tagI = nullptr;

  Tag *m_parentTag = nullptr;

  LevelLabelWithUpButton *m_parentTagLabel = nullptr;

  LineEditWithSnippet *m_nameLineEdit = nullptr;
};
} // namespace vnotex

#endif // NEWTAGDIALOG_H
