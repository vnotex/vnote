#ifndef VNAVIGATIONMODE_H
#define VNAVIGATIONMODE_H

#include <QChar>
#include <QVector>
#include <QMap>
#include <QList>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QTreeWidget;
class QTreeWidgetItem;

// Interface class for Navigation Mode in Captain Mode.
class VNavigationMode
{
public:
    VNavigationMode();

    virtual ~VNavigationMode();

    virtual void registerNavigation(QChar p_majorKey);

    virtual void showNavigation() = 0;

    virtual void hideNavigation();

    // Return true if this object could consume p_key.
    // p_succeed indicates whether the keys hit a target successfully.
    virtual bool handleKeyNavigation(int p_key, bool &p_succeed) = 0;

protected:
    void clearNavigation();

    void showNavigation(QListWidget *p_widget);

    void showNavigation(QTreeWidget *p_widget);

    bool handleKeyNavigation(QListWidget *p_widget,
                             int p_key,
                             bool &p_succeed);

    bool handleKeyNavigation(QTreeWidget *p_widget,
                             int p_key,
                             bool &p_succeed);

    QChar m_majorKey;

    // Map second key to item.
    QMap<QChar, void *> m_keyMap;

    bool m_isSecondKey;

    QVector<QLabel *> m_naviLabels;

private:
    QList<QListWidgetItem *> getVisibleItems(const QListWidget *p_widget) const;

    QList<QTreeWidgetItem *> getVisibleItems(const QTreeWidget *p_widget) const;
};


class VNavigationModeListWidgetWrapper : public QObject, public VNavigationMode
{
    Q_OBJECT
public:
    explicit VNavigationModeListWidgetWrapper(QListWidget *p_widget, QObject *p_parent = nullptr)
        : QObject(p_parent),
          m_widget(p_widget)
    {
    }

    // Implementations for VNavigationMode.
    void showNavigation() Q_DECL_OVERRIDE
    {
        VNavigationMode::showNavigation(m_widget);
    }

    bool handleKeyNavigation(int p_key, bool &p_succeed) Q_DECL_OVERRIDE
    {
        return VNavigationMode::handleKeyNavigation(m_widget,
                                                    p_key,
                                                    p_succeed);
    }

private:
    QListWidget *m_widget;
};
#endif // VNAVIGATIONMODE_H
