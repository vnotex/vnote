#ifndef SNIPPET_H
#define SNIPPET_H

#include <QString>
#include <QJsonObject>

namespace vnotex
{
    class Snippet
    {
    public:
        enum class Type
        {
            Invalid,
            Text,
            Script,
            Dynamic
        };

        enum { InvalidShortcut = -1 };

        Snippet() = default;

        explicit Snippet(const QString &p_name);

        Snippet(const QString &p_name,
                const QString &p_description,
                const QString &p_content,
                int p_shortcut,
                bool p_indentAsFirstLine,
                const QString &p_cursorMark,
                const QString &p_selectionMark);

        virtual ~Snippet() = default;

        QJsonObject toJson() const;
        void fromJson(const QJsonObject &p_jobj);

        bool isValid() const;

        bool isReadOnly() const;

        void setReadOnly(bool p_readOnly);

        const QString &getName() const;

        const QString &getDescription() const;

        Type getType() const;

        int getShortcut() const;

        QString getShortcutString() const;

        const QString &getCursorMark() const;

        const QString &getSelectionMark() const;

        bool isIndentAsFirstLineEnabled() const;

        const QString &getContent() const;

        // Apply the snippet to generate result text.
        virtual QString apply(const QString &p_selectedText,
                              const QString &p_indentationSpaces,
                              int &p_cursorOffset);

        static const QString c_defaultCursorMark;

        static const QString c_defaultSelectionMark;

    protected:
        void setType(Type p_type);

    private:
        bool m_readOnly = false;

        Type m_type = Type::Invalid;

        // Name (and file name) of the snippet.
        // To avoid mixed with shortcut, the name should not contain digits.
        QString m_name;

        QString m_description;

        // Content of the snippet if it is Text.
        // Embedded snippet is supported.
        QString m_content;

        // Shortcut digits of this snippet.
        int m_shortcut = InvalidShortcut;

        bool m_indentAsFirstLine = false;

        // CursorMark is a mark string to indicate the cursor position after applying the snippet.
        QString m_cursorMark;

        // SelectionMark is a mark string which will be replaced by the selected text before applying the snippet after a snippet is applied.
        QString m_selectionMark;
    };
}

#endif // SNIPPET_H
