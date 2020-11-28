#ifndef WEBVIEWER_H
#define WEBVIEWER_H

#include <QWebEngineView>

namespace vnotex
{
    class WebViewer : public QWebEngineView
    {
        Q_OBJECT
    public:
        explicit WebViewer(QWidget *p_parent = nullptr);

        virtual ~WebViewer();
    };
}

#endif // WEBVIEWER_H
