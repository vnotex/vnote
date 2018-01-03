#ifndef VWAITINGWIDGET_H
#define VWAITINGWIDGET_H

#include <QWidget>

class QLabel;


class VWaitingWidget : public QWidget
{
    Q_OBJECT
public:
    explicit VWaitingWidget(QWidget *p_parent = nullptr);

private:
    void setupUI();
};

#endif // VWAITINGWIDGET_H
