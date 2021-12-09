#include "fakeaccessible.h"

#include <QAccessible>
#include <QDebug>

using namespace vnotex;

QAccessibleInterface *FakeAccessible::accessibleFactory(const QString &p_className, QObject *p_obj)
{
    // Try to fix non-responsible issue caused by Youdao Dict.
    if (p_className == QLatin1String("vnotex::LineEdit")
        || p_className == QLatin1String("vnotex::TitleBar")
        || p_className == QLatin1String("vnotex::NotebookSelector")
        || p_className == QLatin1String("vnotex::TagExplorer")
        || p_className == QLatin1String("vnotex::SearchPanel")
        || p_className == QLatin1String("vnotex::SnippetPanel")
        || p_className == QLatin1String("vnotex::OutlineViewer")
        || p_className == QLatin1String("vnotex::TitleToolBar")
        || p_className == QLatin1String("vnotex::MainWindow")
        || p_className == QLatin1String("vnotex::ViewArea")
        || p_className == QLatin1String("vte::VTextEdit")
        || p_className == QLatin1String("vte::IndicatorsBorder")
        || p_className == QLatin1String("vte::MarkdownEditor")
        || p_className == QLatin1String("vte::VMarkdownEditor")
        || p_className == QLatin1String("vte::VTextEditor")
        || p_className == QLatin1String("vte::ViStatusBar")
        || p_className == QLatin1String("vte::StatusIndicator")
        || p_className == QLatin1String("vte::ScrollBar")) {
        return new FakeAccessibleInterface(p_obj);
    }

    return nullptr;
}

FakeAccessibleInterface::FakeAccessibleInterface(QObject *p_obj)
    : m_object(p_obj)
{
}

QAccessibleInterface *FakeAccessibleInterface::child(int p_index) const
{
    Q_UNUSED(p_index);
    return nullptr;
}

QAccessibleInterface *FakeAccessibleInterface::childAt(int p_x, int p_y) const
{
    Q_UNUSED(p_x);
    Q_UNUSED(p_y);
    return nullptr;
}

int FakeAccessibleInterface::childCount() const
{
    return 0;
}

int FakeAccessibleInterface::indexOfChild(const QAccessibleInterface *p_child) const
{
    Q_UNUSED(p_child);
    return -1;
}

bool FakeAccessibleInterface::isValid() const
{
    return false;
}

QObject *FakeAccessibleInterface::object() const
{
    return m_object;
}

QAccessibleInterface *FakeAccessibleInterface::parent() const
{
    return nullptr;
}

QRect FakeAccessibleInterface::rect() const
{
    return QRect();
}

QAccessible::Role FakeAccessibleInterface::role() const
{
    return QAccessible::NoRole;
}

void FakeAccessibleInterface::setText(QAccessible::Text p_t, const QString &p_text)
{
    Q_UNUSED(p_t);
    Q_UNUSED(p_text);
}

QAccessible::State FakeAccessibleInterface::state() const
{
    QAccessible::State state;
    state.disabled = true;
    return state;
}

QString FakeAccessibleInterface::text(QAccessible::Text p_t) const
{
    Q_UNUSED(p_t);
    return QString();
}
