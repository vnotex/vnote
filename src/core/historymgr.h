#ifndef HISTORYMGR_H
#define HISTORYMGR_H

#include <QObject>
#include <QVector>
#include <QSharedPointer>

#include "noncopyable.h"
#include "historyitem.h"
#include "global.h"

namespace vnotex
{
    class Notebook;

    struct HistoryItemFull
    {
        bool operator<(const HistoryItemFull &p_other) const;

        HistoryItem m_item;

        QString m_notebookName;
    };

    // Combine the history from all notebooks and from SessionConfig.
    // SessionConfig will store history about external files.
    // Also provide stack of files accessed during current session, which could be re-opened
    // via Ctrl+Shit+T.
    class HistoryMgr : public QObject, private Noncopyable
    {
        Q_OBJECT
    public:
        struct LastClosedFile
        {
            QString m_path;

            int m_lineNumber = 0;

            ViewWindowMode m_mode = ViewWindowMode::Read;

            bool m_readOnly = false;
        };

        static HistoryMgr &getInst()
        {
            static HistoryMgr inst;
            return inst;
        }

        const QVector<QSharedPointer<HistoryItemFull>> &getHistory() const;

        void add(const QString &p_path,
                 int p_lineNumber,
                 ViewWindowMode p_mode,
                 bool p_readOnly,
                 Notebook *p_notebook);

        void remove(const QVector<QString> &p_paths, Notebook *p_notebook);

        void clear();

        LastClosedFile popLastClosedFile();

        static void removeHistoryItem(QVector<HistoryItem> &p_history, const QString &p_itemPath);

        static void insertHistoryItem(QVector<HistoryItem> &p_history, const HistoryItem &p_item);

    signals:
        void historyUpdated();

    private:
        HistoryMgr();

        void loadHistory();

        // Sorted by last accessed time ascendingly.
        QVector<QSharedPointer<HistoryItemFull>> m_history;

        void removeFromHistory(const QString &p_itemPath);

        QVector<LastClosedFile> m_lastClosedFiles;

        const bool m_perNotebookHistoryEnabled = false;
    };
}

#endif // HISTORYMGR_H
