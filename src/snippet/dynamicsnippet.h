#ifndef DYNAMICSNIPPET_H
#define DYNAMICSNIPPET_H

#include "snippet.h"

#include <functional>

namespace vnotex
{
    // Snippet based on function.
    // To replace the legacy Magic Word.
    class DynamicSnippet : public Snippet
    {
    public:
        typedef std::function<QString(const QString &)> Callback;

        DynamicSnippet(const QString &p_name,
                       const QString &p_description,
                       const Callback &p_callback);

        QString apply(const QString &p_selectedText,
                      const QString &p_indentationSpaces,
                      int &p_cursorOffset) Q_DECL_OVERRIDE;

    private:
        Callback m_callback;
    };
}

#endif // DYNAMICSNIPPET_H
