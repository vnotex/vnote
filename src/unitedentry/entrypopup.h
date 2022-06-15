#ifndef ENTRYPOPUP_H
#define ENTRYPOPUP_H

#include <QFrame>
#include <QSharedPointer>

namespace vnotex
{
    class EntryPopup : public QFrame
    {
        Q_OBJECT
    public:
        explicit EntryPopup(QWidget *p_parent = nullptr);

        ~EntryPopup();

        void setWidget(const QSharedPointer<QWidget> &p_widget);

    private:
        void takeWidget(QWidget *p_widget);

    private:
        QSharedPointer<QWidget> m_widget = nullptr;
    };
}

#endif // ENTRYPOPUP_H
