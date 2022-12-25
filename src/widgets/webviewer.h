#ifndef WEBVIEWER_H
#define WEBVIEWER_H

#include <QWebEngineView>

#include <core/global.h>

namespace vnotex
{
    class WebViewer : public QWebEngineView
    {
        Q_OBJECT
    public:
        WebViewer(const QColor &p_background, qreal p_zoomFactor, QWidget *p_parent = nullptr);

        WebViewer(const QColor &p_background, QWidget *p_parent = nullptr);

        virtual ~WebViewer();

        void findText(const QString &p_text, FindOptions p_options);

    signals:
        void linkHovered(const QString &p_url);
    };
}

#endif // WEBVIEWER_H
