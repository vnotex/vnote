#ifndef LOCATIONLIST_H
#define LOCATIONLIST_H

#include <functional>

#include <QFrame>
#include <QScopedPointer>
#include <QSharedPointer>
#include <QIcon>

#include <core/location.h>

#include "navigationmodewrapper.h"

namespace vnotex
{
    class TitleBar;

    class LocationList : public QFrame
    {
        Q_OBJECT
    public:
        typedef std::function<void(const Location &)> LocationCallback;

        explicit LocationList(QWidget *p_parent = nullptr);

        void clear();

        void addLocation(const ComplexLocation &p_location);

        // Start a new session of the location list to set a callback for activation handling.
        void startSession(const LocationCallback &p_callback);

        QByteArray saveState() const;
        void restoreState(const QByteArray &p_data);

    private:
        enum Columns
        {
            PathColumn = 0,
            LineColumn,
            TextColumn
        };

        void setupUI();

        void setupTitleBar(const QString &p_title, QWidget *p_parent = nullptr);

        void setItemLocationLineAndText(QTreeWidgetItem *p_item, const ComplexLocation::Line &p_line);

        const QIcon &getItemIcon(LocationType p_type);

        Location getItemLocation(const QTreeWidgetItem *p_item) const;

        void updateItemsCountLabel();

        TitleBar *m_titleBar = nullptr;

        QTreeWidget *m_tree = nullptr;

        QScopedPointer<NavigationModeWrapper<QTreeWidget, QTreeWidgetItem>> m_navigationWrapper;

        LocationCallback m_callback;

        static QIcon s_bufferIcon;

        static QIcon s_fileIcon;

        static QIcon s_folderIcon;

        static QIcon s_notebookIcon;
    };
}

#endif // LOCATIONLIST_H
