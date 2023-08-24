#include "settingspage.h"

#include <utils/widgetutils.h>
#include <widgets/propertydefs.h>

using namespace vnotex;

SettingsPage::SettingsPage(QWidget *p_parent)
    : QWidget(p_parent)
{
}

SettingsPage::~SettingsPage()
{
}

bool SettingsPage::search(const QString &p_key)
{
    bool hit = false;

    if (!p_key.isEmpty() && title().contains(p_key, Qt::CaseInsensitive)) {
        hit = true;
    }

    for (const auto& item : m_searchItems) {
        if (!p_key.isEmpty() && item.m_words.contains(p_key, Qt::CaseInsensitive)) {
            // Continue to search all the matched targets.
            hit = true;
            searchHit(item.m_target, true);
        } else {
            searchHit(item.m_target, false);
        }
    }

    return hit;
}

void SettingsPage::searchHit(QWidget *p_target, bool p_hit)
{
    if (!p_target) {
        return;
    }

    if (p_target->property(PropertyDefs::c_hitSettingWidget).toBool() == p_hit) {
        return;
    }

    WidgetUtils::setPropertyDynamically(p_target, PropertyDefs::c_hitSettingWidget, p_hit);
}

void SettingsPage::addSearchItem(const QString &p_words, QWidget *p_target)
{
    m_searchItems.push_back(SearchItem(p_words, p_target));
}

void SettingsPage::addSearchItem(const QString &p_name, const QString &p_tooltip, QWidget *p_target)
{
    addSearchItem(p_name + QStringLiteral(" ") + p_tooltip, p_target);
}

void SettingsPage::pageIsChanged()
{
    m_changed = true;
    emit changed();
}

void SettingsPage::pageIsChangedWithRestartNeeded()
{
    m_changed = true;
    m_restartNeeded = true;
    emit changed();
}

void SettingsPage::load()
{
    m_loading = true;
    loadInternal();
    m_loading = false;
    m_changed = false;
    m_restartNeeded = false;
}

bool SettingsPage::save()
{
    if (!m_changed) {
        return true;
    }

    if (saveInternal()) {
        m_changed = false;
        return true;
    }

    return false;
}

void SettingsPage::reset()
{
    if (!m_changed) {
        return;
    }

    load();
}

const QString &SettingsPage::error() const
{
    return m_error;
}

void SettingsPage::setError(const QString &p_err)
{
    m_error = p_err;
}

bool SettingsPage::isRestartNeeded() const
{
    return m_restartNeeded;
}

bool SettingsPage::isLoading() const
{
    return m_loading;
}
