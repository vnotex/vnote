#ifndef SNIPPETMGR_H
#define SNIPPETMGR_H

#include <QObject>
#include <QVector>
#include <QMap>
#include <QSharedPointer>

#include <core/noncopyable.h>

#include "dynamicsnippet.h"

namespace vte
{
    class VTextEdit;
}

namespace vnotex
{
    class Buffer;

    class SnippetMgr : public QObject, private Noncopyable
    {
        Q_OBJECT
    public:
        typedef QMap<QString, QString> OverrideMap;

        static SnippetMgr &getInst()
        {
            static SnippetMgr inst;
            return inst;
        }

        QString getSnippetFolder() const;

        const QVector<QSharedPointer<Snippet>> &getSnippets() const;

        // @p_exemption: include it even it is occupied by one snippet.
        QVector<int> getAvailableShortcuts(int p_exemption = Snippet::InvalidShortcut) const;

        QSharedPointer<Snippet> find(const QString &p_name, Qt::CaseSensitivity p_cs = Qt::CaseSensitive) const;

        void addSnippet(const QSharedPointer<Snippet> &p_snippet);

        void removeSnippet(const QString &p_name);

        void updateSnippet(const QString &p_name, const QSharedPointer<Snippet> &p_snippet);

        // Apply snippet @p_name directly in current cursor position.
        // For snippets in @p_overrides, we just provide simple contents without nested snippets.
        void applySnippet(const QString &p_name,
                          vte::VTextEdit *p_textEdit,
                          const OverrideMap &p_overrides = OverrideMap()) const;

        // Resolve %snippet_name% as snippet and apply recursively.
        // Will update @p_cursorOffset if needed.
        // For snippets in @p_overrides, we just provide simple contents without nested snippets.
        QString applySnippetBySymbol(const QString &p_content,
                                     const QString &p_selectedText,
                                     int &p_cursorOffset,
                                     const OverrideMap &p_overrides = OverrideMap()) const;

        QString applySnippetBySymbol(const QString &p_content) const;

        // Generate standard overrides for given buffer.
        static OverrideMap generateOverrides(const Buffer *p_buffer);

        // Generate standard overrides.
        static OverrideMap generateOverrides(const QString &p_fileName);

        // %name%.
        // Captured texts:
        // 1 - The name of the snippet.
        static const QString c_snippetSymbolRegExp;

    private:
        SnippetMgr();

        void loadSnippets();

        QSharedPointer<Snippet> loadSnippet(const QString &p_snippetFile) const;

        void saveSnippet(const QSharedPointer<Snippet> &p_snippet);

        QString getSnippetFile(const QSharedPointer<Snippet> &p_snippet) const;

        void addOneSnippet(const QSharedPointer<Snippet> &p_snippet);

        void removeSnippetFromShortcutMap(const QSharedPointer<Snippet> &p_snippet);

        void addSnippetToShortcutMap(const QSharedPointer<Snippet> &p_snippet);

        QVector<QSharedPointer<Snippet>> loadBuiltInSnippets() const;

        static void addDynamicSnippet(QVector<QSharedPointer<Snippet>> &p_snippets,
                                      const QString &p_name,
                                      const QString &p_description,
                                      const DynamicSnippet::Callback &p_callback);

        QVector<QSharedPointer<Snippet>> m_snippets;

        QMap<int, QSharedPointer<Snippet>> m_shortcutToSnippet;
    };
}

#endif // SNIPPETMGR_H
