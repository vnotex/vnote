#ifndef SELECTDIALOG_H
#define SELECTDIALOG_H

#include <QDialog>
#include <QMap>

class QPushButton;
class QMouseEvent;
class QListWidget;
class QListWidgetItem;
class QShowEvent;
class QKeyEvent;
class QLabel;

namespace vnotex {
class SelectDialog : public QDialog {
  Q_OBJECT
public:
  SelectDialog(const QString &p_title, QWidget *p_parent = nullptr);

  SelectDialog(const QString &p_title, const QString &p_text, QWidget *p_parent = nullptr);

  // @p_selectID should >= 0.
  void addSelection(const QString &p_selectStr, int p_selectID);

  int getSelection() const;

  bool eventFilter(QObject *p_watched, QEvent *p_event) Q_DECL_OVERRIDE;

protected:
  void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

  void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;

private slots:
  void selectionChosen(QListWidgetItem *p_item);

private:
  enum { CANCEL_ID = -1 };

  void setupUI(const QString &p_title, const QString &p_text = QString());

  void updateSize();

  int m_choice = CANCEL_ID;

  QLabel *m_label = nullptr;

  QListWidget *m_list = nullptr;

  QMap<QChar, QListWidgetItem *> m_shortcuts;

  QString m_shortcutIconForeground;

  QString m_shortcutIconBorder;

  QChar m_nextShortcut = QLatin1Char('a');

  static const QChar c_cancelShortcut;
};
} // namespace vnotex

#endif // SELECTDIALOG_H
