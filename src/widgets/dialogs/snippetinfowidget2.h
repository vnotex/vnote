#ifndef SNIPPETINFOWIDGET2_H
#define SNIPPETINFOWIDGET2_H

#include <QWidget>

class QLineEdit;
class QComboBox;
class QCheckBox;
class QPlainTextEdit;

namespace vnotex {
class ServiceLocator;

class SnippetInfoWidget2 : public QWidget {
  Q_OBJECT
public:
  // Create mode.
  explicit SnippetInfoWidget2(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  // Edit mode — pre-fill from existing snippet data.
  SnippetInfoWidget2(ServiceLocator &p_services, const QJsonObject &p_snippetData,
                     QWidget *p_parent = nullptr);

  QString getName() const;

  QString getDescription() const;

  QString getType() const;

  int getShortcut() const;

  QJsonObject toContentJson() const;

  void setReadOnly(bool p_readOnly);

  void populateShortcuts(const QList<int> &p_available, int p_current = -1);

signals:
  void inputEdited();

private:
  void setupUI();

  void setupTypeComboBox();

  void setupShortcutComboBox();

  ServiceLocator &m_services;

  bool m_editMode = false;

  QLineEdit *m_nameLineEdit = nullptr;

  QLineEdit *m_descriptionLineEdit = nullptr;

  QComboBox *m_typeComboBox = nullptr;

  QComboBox *m_shortcutComboBox = nullptr;

  QLineEdit *m_cursorMarkLineEdit = nullptr;

  QLineEdit *m_selectionMarkLineEdit = nullptr;

  QCheckBox *m_indentAsFirstLineCheckBox = nullptr;

  QPlainTextEdit *m_contentTextEdit = nullptr;
};
} // namespace vnotex

#endif // SNIPPETINFOWIDGET2_H
