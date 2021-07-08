#include "dynamicsnippet.h"

#include <QDebug>

using namespace vnotex;

DynamicSnippet::DynamicSnippet(const QString &p_name,
                               const QString &p_description,
                               const Callback &p_callback)
    : Snippet(p_name,
              p_description,
              QString(),
              Snippet::InvalidShortcut,
              false,
              QString(),
              QString()),
      m_callback(p_callback)
{
    setType(Type::Dynamic);
    setReadOnly(true);
}

QString DynamicSnippet::apply(const QString &p_selectedText,
                              const QString &p_indentationSpaces,
                              int &p_cursorOffset)
{
    Q_UNUSED(p_indentationSpaces);
    auto text = m_callback(p_selectedText);
    p_cursorOffset = text.size();
    return text;
}
