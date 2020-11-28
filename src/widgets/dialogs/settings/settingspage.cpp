#include "settingspage.h"

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
    for (const auto& item : m_searchItems) {
        if (item.m_words.contains(p_key, Qt::CaseInsensitive)) {
            // Continue to search all the matched targets.
            hit = true;
            searchHit(item.m_target);
        }
    }

    return hit;
}

void SettingsPage::searchHit(QWidget *p_target)
{
    Q_UNUSED(p_target);
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

void SettingsPage::load()
{
    loadInternal();
    m_changed = false;
}

void SettingsPage::save()
{
    if (!m_changed) {
        return;
    }

    saveInternal();
    m_changed = false;
}

void SettingsPage::reset()
{
    if (!m_changed) {
        return;
    }

    load();
}
