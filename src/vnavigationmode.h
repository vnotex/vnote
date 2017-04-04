#ifndef VNAVIGATIONMODE_H
#define VNAVIGATIONMODE_H

#include <QChar>

// Interface class for Navigation Mode in Captain Mode.
class VNavigationMode
{
public:
    VNavigationMode() {};
    virtual ~VNavigationMode() {};

    virtual void registerNavigation(QChar p_majorKey) = 0;
    virtual void showNavigation() = 0;
    virtual void hideNavigation() = 0;
    // Return true if this object could consume p_key.
    // p_succeed indicates whether the keys hit a target successfully.
    virtual bool handleKeyNavigation(int p_key, bool &p_succeed) = 0;

protected:
    QChar m_majorKey;
};

#endif // VNAVIGATIONMODE_H
