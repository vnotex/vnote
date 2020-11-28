#ifndef ICONUTILS_H
#define ICONUTILS_H

#include <QPixmap>
#include <QVector>
#include <QIcon>

namespace vnotex
{
    class IconUtils
    {
    public:
        IconUtils() = delete;

        struct OverriddenColor
        {
            OverriddenColor()
                : m_mode(QIcon::Normal),
                  m_state(QIcon::Off)
            {
            }

            OverriddenColor(const QString &p_foreground,
                            QIcon::Mode p_mode = QIcon::Normal,
                            QIcon::State p_state = QIcon::Off)
                : m_foreground(p_foreground),
                  m_mode(p_mode),
                  m_state(p_state)
            {
            }

            QString m_foreground;
            QIcon::Mode m_mode;
            QIcon::State m_state;
        };

        static void setDefaultIconForeground(const QString &p_fg, const QString &p_disabledFg);

        static QIcon fetchIcon(const QString &p_iconFile,
                               const QVector<OverriddenColor> &p_overriddenColors);

        static QIcon fetchIcon(const QString &p_iconFile, const QString &p_overriddenForeground);

        // Fetch icon from @p_iconFile with icon_fg as overridden foreground color.
        static QIcon fetchIcon(const QString &p_iconFile);

        static QIcon fetchIconWithDisabledState(const QString &p_iconFile);

    private:
        static QString replaceForegroundOfIcon(const QString &p_iconContent, const QString &p_foreground);

        static QString s_defaultIconForeground;

        static QString s_defaultIconDisabledForeground;
    };
} // ns vnotex

#endif // ICONUTILS_H
