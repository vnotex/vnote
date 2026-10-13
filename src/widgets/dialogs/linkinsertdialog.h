#ifndef LINKINSERTDIALOG_H
#define LINKINSERTDIALOG_H

#include "scrolldialog.h"

class QLineEdit;

namespace vnotex {
class LinkInsertDialog : public ScrollDialog {
  Q_OBJECT
public:
  LinkInsertDialog(const QString &p_title, const QString &p_linkText, const QString &p_linkUrl,
                   bool p_linkTextOptional, QWidget *p_parent = nullptr);

  QString getLinkText() const;

  QString getLinkUrl() const;

  // Edit only the URL. An empty URL can optionally remove an existing link.
  void setUrlOnly(bool p_allowEmpty = false);

protected:
  void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

private slots:
  void checkInput(bool p_autoCompleteText = true);

private:
  void setupUI(const QString &p_title, const QString &p_linkText, const QString &p_linkUrl);

  bool m_linkTextOptional = false;
  bool m_urlOnly = false;
  bool m_allowEmptyUrl = false;

  QLineEdit *m_linkTextEdit = nullptr;

  QLineEdit *m_linkUrlEdit = nullptr;
};
} // namespace vnotex

#endif // LINKINSERTDIALOG_H
