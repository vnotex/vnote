#include "fakeaccessible.h"

#include <QAccessible>
#include <QDebug>

using namespace vnotex;

QAccessibleInterface *FakeAccessible::accessibleFactory(const QString &p_className,
                                                        QObject *p_obj) {
  // Try to fix non-responsible issue caused by Youdao Dict.
  if (p_className.startsWith(QStringLiteral("vnotex::")) ||
      p_className.startsWith(QStringLiteral("vte::"))) {
    // Qt's docs: All interfaces are managed by an internal cache and should not be deleted.
    return new FakeAccessibleInterface(p_obj);
  }

  return nullptr;
}

FakeAccessibleInterface::FakeAccessibleInterface(QObject *p_obj) : m_object(p_obj) {}

QAccessibleInterface *FakeAccessibleInterface::child(int p_index) const {
  Q_UNUSED(p_index);
  return nullptr;
}

QAccessibleInterface *FakeAccessibleInterface::childAt(int p_x, int p_y) const {
  Q_UNUSED(p_x);
  Q_UNUSED(p_y);
  return nullptr;
}

int FakeAccessibleInterface::childCount() const { return 0; }

int FakeAccessibleInterface::indexOfChild(const QAccessibleInterface *p_child) const {
  Q_UNUSED(p_child);
  return -1;
}

bool FakeAccessibleInterface::isValid() const { return false; }

QObject *FakeAccessibleInterface::object() const { return m_object; }

QAccessibleInterface *FakeAccessibleInterface::parent() const { return nullptr; }

QRect FakeAccessibleInterface::rect() const { return QRect(); }

QAccessible::Role FakeAccessibleInterface::role() const { return QAccessible::NoRole; }

void FakeAccessibleInterface::setText(QAccessible::Text p_t, const QString &p_text) {
  Q_UNUSED(p_t);
  Q_UNUSED(p_text);
}

QAccessible::State FakeAccessibleInterface::state() const {
  QAccessible::State state;
  state.disabled = true;
  return state;
}

QString FakeAccessibleInterface::text(QAccessible::Text p_t) const {
  Q_UNUSED(p_t);
  return QString();
}
