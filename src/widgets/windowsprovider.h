#ifndef WINDOWSPROVIDER_H
#define WINDOWSPROVIDER_H

#include <QObject>

#include <QVector>

#include <core/global.h>

namespace vnotex
{
    class ViewArea;

    class WindowsProvider : public QObject
    {
        Q_OBJECT
    public:
        struct WindowData
        {
            int m_index = -1;

            QString m_name;
        };

        struct ViewSplitWindows
        {
            ID m_viewSplitId = 0;

            QVector<WindowData> m_viewWindows;
        };

        explicit WindowsProvider(ViewArea *p_viewArea);

        QVector<ViewSplitWindows> getWindows() const;

        void setCurrentWindow(ID p_viewSplitId, int p_windowIndex);

    signals:
        void windowsChanged();

    private:
        ViewArea *m_viewArea = nullptr;
    };
}

#endif // WINDOWSPROVIDER_H
