#include "vwaitingwidget.h"

#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>
#include <QHBoxLayout>

VWaitingWidget::VWaitingWidget(QWidget *p_parent)
    : QWidget(p_parent)
{
    setupUI();
}

void VWaitingWidget::setupUI()
{
    QSize imgSize(64, 64);
    QLabel *logoLabel = new QLabel();
    logoLabel->setPixmap(QPixmap(":/resources/icons/vnote.svg").scaled(imgSize, Qt::KeepAspectRatio));

    QHBoxLayout *layout = new QHBoxLayout();
    layout->addStretch();
    layout->addWidget(logoLabel);
    layout->addStretch();

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addStretch();
    mainLayout->addLayout(layout);
    mainLayout->addStretch();

    mainLayout->setContentsMargins(0, 0, 0, 0);

    setLayout(mainLayout);
}
