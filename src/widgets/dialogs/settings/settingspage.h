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

        bool save();

        void reset();

        virtual QString title() const = 0;

        bool search(const QString &p_key);

        const QString &error() const;

        bool isRestartNeeded() const;

    signals:
        void changed();

    protected:
        virtual void loadInternal() = 0;

        virtual bool saveInternal() = 0;

        // Subclass could override this method to highlight matched target.
        virtual void searchHit(QWidget *p_target, bool p_hit);

        void addSearchItem(const QString &p_words, QWidget *p_target);

        void addSearchItem(const QString &p_name, const QString &p_tooltip, QWidget *p_target);

        void setError(const QString &p_err);

        bool isLoading() const;

    protected slots:
        void pageIsChanged();

        void pageIsChangedWithRestartNeeded();

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

        bool m_restartNeeded = false;

        bool m_loading = false;

        QString m_error;
    };
}

#endif // SETTINGSPAGE_H
