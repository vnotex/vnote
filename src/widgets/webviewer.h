#ifndef WEBVIEWER_H
#define WEBVIEWER_H

#include <QWebEngineView>

namespace vnotex
{
    class WebViewer : public QWebEngineView
    {
        Q_OBJECT
    public:
        explicit WebViewer(const QColor &p_background,
                           qreal p_zoomFactor,
                           QWidget *p_parent = nullptr);

        virtual ~WebViewer();

    signals:
        void linkHovered(const QString &p_url);
    };
}

#endif // WEBVIEWER_H
