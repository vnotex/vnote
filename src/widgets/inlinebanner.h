#ifndef INLINEBANNER_H
#define INLINEBANNER_H

#include <QFrame>
#include <QString>
#include <QVector>

class QHBoxLayout;
class QLabel;
class QPushButton;

namespace vnotex {

// A themed, inline notification strip: one wrapping message plus zero or more
// trailing action buttons.
//
// Pure VIEW. It renders what it is told and hands back the buttons it created;
// it owns no policy, no services and no business logic. Consumers connect the
// returned buttons and keep every decision on their own side.
//
// Intended hosts: ViewWindow2::addTopWidget() for an in-editor strip, or any
// QLayout for a panel/dialog banner.
//
// THEMING: never call setStyleSheet() on this widget. Colors come from each
// theme's interface.qss via the `vnotex--InlineBanner` selector and the
// `BannerSeverity` dynamic property (PropertyDefs::c_bannerSeverity). The
// stylesheet is applied globally on QApplication and re-applied on theme
// change, so an instance re-themes itself with no code here.
//
// Deliberately NOT provided: a built-in close button (a consumer that wants one
// adds it through addActionButton), icons, auto-hide timers, and stacking or
// priority across multiple banners. Add those when a second consumer actually
// needs them.
class InlineBanner : public QFrame {
  Q_OBJECT

public:
  enum class Severity {
    Info,    // Neutral, informational.
    Warning, // Something needs attention but nothing is broken.
    Error    // An operation failed.
  };
  Q_ENUM(Severity)

  explicit InlineBanner(QWidget *p_parent = nullptr);

  InlineBanner(Severity p_severity, const QString &p_text, QWidget *p_parent = nullptr);

  void setSeverity(Severity p_severity);
  Severity getSeverity() const;

  void setText(const QString &p_text);
  QString getText() const;

  // Appends a button to the right of the message and returns it, so the caller
  // owns the semantics (Qt's QDialogButtonBox::addButton convention). The
  // banner keeps ownership; the pointer is valid until clearActionButtons() or
  // the banner is destroyed.
  QPushButton *addActionButton(const QString &p_text);

  void clearActionButtons();

  QVector<QPushButton *> getActionButtons() const;

  // QSS property value for a severity. Public so themes, tests and consumers
  // all read the same source of truth.
  static QString severityName(Severity p_severity);

private:
  void setupUI();

  void applySeverity();

  Severity m_severity = Severity::Info;

  QHBoxLayout *m_layout = nullptr;

  QLabel *m_label = nullptr;

  QVector<QPushButton *> m_actionButtons;
};

} // namespace vnotex

#endif // INLINEBANNER_H
