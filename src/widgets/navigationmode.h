#ifndef NAVIGATIONMODE_H
#define NAVIGATIONMODE_H

#include <QMap>
#include <QChar>
#include <QVector>

#include <utils/utils.h>

class QListWidget;
class QListWidgetItem;
class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QWidget;

namespace vnotex
{
    // Interface for Navigation Mode.
    // Need to inherit this class if one widget wants to support Navigation mode.
    class NavigationMode
    {
    public:
        struct Status
        {
            bool m_isKeyConsumed = false;
            bool m_isTargetHit = false;
        };

        virtual ~NavigationMode() {}

        virtual void registerNavigation(QChar p_majorKey);

        virtual void showNavigation();

        virtual void hideNavigation();

        virtual NavigationMode::Status handleKeyNavigation(int p_key);

    protected:
        enum class Type
        {
            SingleKey,
            DoubleKeys,
            StagedDoubleKeys
        };

        explicit NavigationMode(NavigationMode::Type p_type, QWidget *p_widget);

        // @p_item: if it is null, that means it is a major key hit; otherwise, it is a second key hit.
        virtual void handleTargetHit(void *p_item) = 0;

        virtual bool isTargetVisible();

        virtual QVector<void *> getVisibleNavigationItems();

        // @p_idx: -1 for SingleKey and the major stage of StagedDoubleKeys.
        virtual void placeNavigationLabel(int p_idx, void * p_item, QLabel *p_label) = 0;

        virtual void showNavigationWithDoubleKeys();

        virtual void clearNavigation();

        // a-z and 0-9 are allowed for second key.
        static const int c_maxNumOfNavigationItems = 36;

    private:
        QString generateLabelString(QChar p_secondKey) const;

        QLabel *createNavigationLabel(QChar p_secondKey, QWidget *p_parent) const;

        static QChar generateSecondKey(int p_idx);

        Type m_type = Type::SingleKey;

        QWidget *m_widget = nullptr;

        QChar m_majorKey;

        QMap<QChar, void *> m_secondKeyMap;

        bool m_isMajorKeyConsumed = false;

        QVector<QLabel *> m_navigationLabels;
    };
} // ns vnotex

#endif // NAVIGATIONMODE_H
