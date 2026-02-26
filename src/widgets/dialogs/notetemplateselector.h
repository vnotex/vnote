#ifndef NOTETEMPLATESELECTOR_H
#define NOTETEMPLATESELECTOR_H

#include <QWidget>

class QComboBox;
class QPlainTextEdit;


namespace vnotex {

class ServiceLocator;
class NoteTemplateSelector : public QWidget {
  Q_OBJECT
public:
  explicit NoteTemplateSelector(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  QString getCurrentTemplate() const;
  bool setCurrentTemplate(const QString &p_template);

  const QString &getTemplateContent() const;

signals:
  void templateChanged();

private:
  void setupUI();

  void setupTemplateComboBox(QWidget *p_parent);

  void updateCurrentTemplate();

  QComboBox *m_templateComboBox = nullptr;

  QPlainTextEdit *m_templateTextEdit = nullptr;

  QString m_templateContent;

  ServiceLocator &m_services;
};
} // namespace vnotex

#endif // NOTETEMPLATESELECTOR_H
