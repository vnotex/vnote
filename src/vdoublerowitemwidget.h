#ifndef VDOUBLEROWITEMWIDGET_H
#define VDOUBLEROWITEMWIDGET_H

#include <QWidget>

class QLabel;

class VDoubleRowItemWidget : public QWidget
{
    Q_OBJECT
public:
    explicit VDoubleRowItemWidget(QWidget *p_parent = nullptr);

    void setText(const QString &p_firstText, const QString &p_secondText);

private:
    QLabel *m_firstLabel;
    QLabel *m_secondLabel;
};

#endif // VDOUBLEROWITEMWIDGET_H
