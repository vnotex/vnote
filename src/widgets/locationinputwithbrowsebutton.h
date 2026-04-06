#ifndef LOCATIONINPUTWITHBROWSEBUTTON_H
#define LOCATIONINPUTWITHBROWSEBUTTON_H

#include <QWidget>

class QLineEdit;
class QPushButton;

namespace vnotex {
class LocationInputWithBrowseButton : public QWidget {
  Q_OBJECT
public:
  explicit LocationInputWithBrowseButton(QWidget *p_parent = nullptr);

  QString text() const;

  void setText(const QString &p_text);

  QString toolTip() const;

  void setToolTip(const QString &p_tip);

  void setPlaceholderText(const QString &p_text);

  // Returns the cached browse path, or QDir::homePath() if none cached.
  static QString defaultBrowsePath();

signals:
  void clicked();

  void textChanged(const QString &p_text);

private:
  QLineEdit *m_lineEdit = nullptr;

  static QString s_lastBrowsePath;
};
} // namespace vnotex

#endif // LOCATIONINPUTWITHBROWSEBUTTON_H
