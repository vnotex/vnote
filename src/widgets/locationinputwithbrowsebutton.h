#ifndef LOCATIONINPUTWITHBROWSEBUTTON_H
#define LOCATIONINPUTWITHBROWSEBUTTON_H

#include <QWidget>

class QLineEdit;
class QPushButton;

namespace vnotex {
class LocationInputWithBrowseButton : public QWidget {
  Q_OBJECT
public:
  enum BrowseType { File, Folder };

  explicit LocationInputWithBrowseButton(QWidget *p_parent = nullptr);

  QString text() const;

  void setText(const QString &p_text);

  QString toolTip() const;

  void setToolTip(const QString &p_tip);

  void setPlaceholderText(const QString &p_text);

  // Set browse type and optional dialog title/filter.
  // When set, the widget handles the browse dialog internally.
  void setBrowseType(BrowseType p_type,
                     const QString &p_title = QString(),
                     const QString &p_filter = QString());

signals:
  // Emitted when browse button is clicked and no BrowseType is configured.
  void clicked();

  void textChanged(const QString &p_text);

private:
  void browse();

  // Returns cached browse path, or QDir::homePath() if none.
  static QString defaultBrowsePath();

  QLineEdit *m_lineEdit = nullptr;

  BrowseType m_browseType = File;
  QString m_browseTitle;
  QString m_browseFilter;
  bool m_browseConfigured = false;

  static QString s_lastBrowsePath;
};
} // namespace vnotex

#endif // LOCATIONINPUTWITHBROWSEBUTTON_H
