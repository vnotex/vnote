#ifndef VSNIPPET_H
#define VSNIPPET_H

#include <QString>
#include <QJsonObject>
#include <QTextCursor>


class VSnippet
{
public:
    enum Type
    {
        PlainText = 0,
        Html,
        Invalid
    };

    VSnippet();

    VSnippet(const QString &p_name,
             Type p_type = Type::PlainText,
             const QString &p_content = QString(),
             const QString &p_cursorMark = c_defaultCursorMark,
             const QString &p_selectionMark = c_defaultSelectionMark,
             QChar p_shortcut = QChar(),
             bool p_autoIndent = false);

    // Return true if there is any update.
    bool update(const QString &p_name,
                Type p_type,
                const QString &p_content,
                const QString &p_cursorMark,
                const QString &p_selectionMark,
                QChar p_shortcut,
                bool p_autoIndent);

    const QString &getName() const
    {
        return m_name;
    }

    VSnippet::Type getType() const
    {
        return m_type;
    }

    const QString &getCursorMark() const
    {
        return m_cursorMark;
    }

    const QString &getSelectionMark() const
    {
        return m_selectionMark;
    }

    const QString &getContent() const
    {
        return m_content;
    }

    QChar getShortcut() const
    {
        return m_shortcut;
    }

    bool getAutoIndent() const
    {
        return m_autoIndent;
    }

    void setContent(const QString &p_content)
    {
        m_content = p_content;
    }

    // Not including m_content.
    QJsonObject toJson() const;

    // Apply this snippet via @p_cursor.
    bool apply(QTextCursor &p_cursor) const;

    // Not including m_content.
    static VSnippet fromJson(const QJsonObject &p_json);

    static QString typeStr(VSnippet::Type p_type);

    static const QVector<QChar> &getAllShortcuts();

    static bool isValidShortcut(QChar p_char);

private:
    // File name in the snippet folder.
    QString m_name;

    Type m_type;

    // Support magic word.
    QString m_content;

    // String in the content that mark the position of the cursor after insertion.
    // If there is no such mark in the content, the cursor should be put at the
    // end of the insertion.
    QString m_cursorMark;

    // Selection marks in the content will be replaced by selected text.
    QString m_selectionMark;

    // Shortcut to apply this snippet.
    QChar m_shortcut;

    // Auto indent with the first line.
    bool m_autoIndent;

    static const QString c_defaultCursorMark;

    static const QString c_defaultSelectionMark;

    static QVector<QChar> s_allShortcuts;
};

#endif // VSNIPPET_H
