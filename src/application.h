#ifndef APPLICATION_H
#define APPLICATION_H

#include <QApplication>

namespace vnotex
{
    class Application : public QApplication
    {
        Q_OBJECT
    public:
        Application(int &p_argc, char **p_argv);

    signals:
        void openFileRequested(const QString &p_filePath);

    protected:
        bool event(QEvent *p_event) Q_DECL_OVERRIDE;
    };
}

#endif // APPLICATION_H
