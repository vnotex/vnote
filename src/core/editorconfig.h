#ifndef VNOTEX_EDITORCONFIG_H
#define VNOTEX_EDITORCONFIG_H

#include "iconfig.h"

#include <QScopedPointer>
#include <QSharedPointer>
#include <QObject>

namespace vnotex
{
    class TextEditorConfig;
    class MarkdownEditorConfig;

    class EditorConfig : public IConfig
    {
        Q_GADGET
    public:
        enum Shortcut
        {
            Save,
            EditRead,
            Discard,
            TypeHeading1,
            TypeHeading2,
            TypeHeading3,
            TypeHeading4,
            TypeHeading5,
            TypeHeading6,
            TypeHeadingNone,
            TypeBold,
            TypeItalic,
            TypeStrikethrough,
            TypeUnorderedList,
            TypeOrderedList,
            TypeTodoList,
            TypeCheckedTodoList,
            TypeCode,
            TypeCodeBlock,
            TypeMath,
            TypeMathBlock,
            TypeQuote,
            TypeLink,
            TypeImage,
            TypeTable,
            TypeMark,
            Outline,
            RichPaste,
            FindAndReplace,
            FindNext,
            FindPrevious,
            MaxShortcut
        };
        Q_ENUM(Shortcut)

        enum AutoSavePolicy
        {
            None,
            AutoSave,
            BackupFile
        };
        Q_ENUM(AutoSavePolicy)

        EditorConfig(ConfigMgr *p_mgr, IConfig *p_topConfig);

        ~EditorConfig();

        TextEditorConfig &getTextEditorConfig();
        const TextEditorConfig &getTextEditorConfig() const;

        MarkdownEditorConfig &getMarkdownEditorConfig();
        const MarkdownEditorConfig &getMarkdownEditorConfig() const;

        void init(const QJsonObject &p_app, const QJsonObject &p_user) Q_DECL_OVERRIDE;

        QJsonObject toJson() const Q_DECL_OVERRIDE;

        int getToolBarIconSize() const;
        void setToolBarIconSize(int p_size);

        EditorConfig::AutoSavePolicy getAutoSavePolicy() const;
        void setAutoSavePolicy(EditorConfig::AutoSavePolicy p_policy);

        const QString &getBackupFileDirectory() const;

        const QString &getBackupFileExtension() const;

        const QString &getShortcut(Shortcut p_shortcut) const;

    private:
        void loadCore(const QJsonObject &p_app, const QJsonObject &p_user);

        QJsonObject saveCore() const;

        void loadShortcuts(const QJsonObject &p_app, const QJsonObject &p_user);

        QJsonObject saveShortcuts() const;

        QString autoSavePolicyToString(AutoSavePolicy p_policy) const;
        AutoSavePolicy stringToAutoSavePolicy(const QString &p_str) const;

        // Icon size of editor tool bar.
        int m_toolBarIconSize = 14;

        QString m_shortcuts[Shortcut::MaxShortcut];

        AutoSavePolicy m_autoSavePolicy = AutoSavePolicy::AutoSave;

        // Where to put backup file, relative to the content file itself.
        QString m_backupFileDirectory;

        // Backup file extension.
        QString m_backupFileExtension;

        // Will be shared with MarkdownEditorConfig.
        QSharedPointer<TextEditorConfig> m_textEditorConfig;

        QScopedPointer<MarkdownEditorConfig> m_markdownEditorConfig;
    };
}

#endif // EDITORCONFIG_H
