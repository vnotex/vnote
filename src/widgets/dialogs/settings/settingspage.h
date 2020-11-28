#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H

#include <QWidget>

namespace vnotex
{
    class SettingsPage : public QWidget
    {
        Q_OBJECT
    public:
        explicit SettingsPage(QWidget *p_parent = nullptr);

        virtual ~SettingsPage();

        void load();

        void save();

        void reset();

        virtual QString title() const = 0;

        bool search(const QString &p_key);

    signals:
        void changed();

    protected:
        virtual void loadInternal() = 0;

        virtual void saveInternal() = 0;

        // Subclass could override this method to highlight matched target.
        virtual void searchHit(QWidget *p_target);

        void addSearchItem(const QString &p_words, QWidget *p_target);

        void addSearchItem(const QString &p_name, const QString &p_tooltip, QWidget *p_target);

    protected slots:
        void pageIsChanged();

    private:
        struct SearchItem
        {
            SearchItem() = default;

            SearchItem(const QString &p_words, QWidget *p_target)
                : m_words(p_words),
                  m_target(p_target)
            {
            }

            QString m_words;
            QWidget *m_target = nullptr;
        };

        QVector<SearchItem> m_searchItems;

        bool m_changed = false;
    };
}

#endif // SETTINGSPAGE_H
