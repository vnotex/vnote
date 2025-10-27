#ifndef FAKEACCESSIBLE_H
#define FAKEACCESSIBLE_H

#include <QAccessibleInterface>

class QObject;
class QString;

namespace vnotex {
class FakeAccessible {
public:
  FakeAccessible() = delete;

  static QAccessibleInterface *accessibleFactory(const QString &p_className, QObject *p_obj);
};

class FakeAccessibleInterface : public QAccessibleInterface {
public:
  FakeAccessibleInterface(QObject *p_obj);

  QAccessibleInterface *child(int p_index) const Q_DECL_OVERRIDE;

  QAccessibleInterface *childAt(int p_x, int p_y) const Q_DECL_OVERRIDE;

  int childCount() const Q_DECL_OVERRIDE;

  int indexOfChild(const QAccessibleInterface *p_child) const Q_DECL_OVERRIDE;

  bool isValid() const Q_DECL_OVERRIDE;

  QObject *object() const Q_DECL_OVERRIDE;

  QAccessibleInterface *parent() const Q_DECL_OVERRIDE;

  QRect rect() const Q_DECL_OVERRIDE;

  QAccessible::Role role() const Q_DECL_OVERRIDE;

  void setText(QAccessible::Text p_t, const QString &p_text) Q_DECL_OVERRIDE;

  QAccessible::State state() const Q_DECL_OVERRIDE;

  QString text(QAccessible::Text p_t) const Q_DECL_OVERRIDE;

private:
  QObject *m_object = nullptr;
};
} // namespace vnotex

#endif // FAKEACCESSIBLE_H
