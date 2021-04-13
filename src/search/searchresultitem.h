#ifndef SEARCHRESULTITEM_H
#define SEARCHRESULTITEM_H

#include <QSharedPointer>
#include <QString>
#include <QDebug>

#include <core/location.h>

namespace vnotex
{
    struct SearchResultItem
    {
        friend QDebug operator<<(QDebug p_dbg, const SearchResultItem &p_item)
        {
            p_dbg << p_item.m_location;
            return p_dbg;
        }

        void addLine(int p_lineNumber, const QString &p_text);

        static QSharedPointer<SearchResultItem> createBufferItem(const QString &p_targetPath,
                                                                 const QString &p_displayPath,
                                                                 int p_lineNumber,
                                                                 const QString &p_text);

        static QSharedPointer<SearchResultItem> createFileItem(const QString &p_targetPath,
                                                               const QString &p_displayPath,
                                                               int p_lineNumber,
                                                               const QString &p_text);

        static QSharedPointer<SearchResultItem> createFolderItem(const QString &p_targetPath,
                                                                 const QString &p_displayPath);

        static QSharedPointer<SearchResultItem> createNotebookItem(const QString &p_targetPath,
                                                                   const QString &p_displayPath);

        ComplexLocation m_location;
    };
}

#endif // SEARCHRESULTITEM_H
