#include "labelwithbuttonswidget.h"

#include <QLabel>
#include <QHBoxLayout>
#include <QStyleOption>
#include <QPainter>
#include <QDebug>
#include <QStyle>
#include <QToolButton>
#include <QAction>

#include <core/vnotex.h>
#include <utils/iconutils.h>

using namespace vnotex;

LabelWithButtonsWidget::LabelWithButtonsWidget(const QString &p_label, Buttons p_buttons, QWidget *p_parent)
    : QWidget(p_parent)
{
    setupUI(p_buttons);

    setLabel(p_label);
}

void LabelWithButtonsWidget::setupUI(Buttons p_buttons)
{
    auto mainLayout = new QHBoxLayout(this);

    m_label = new QLabel(this);
    mainLayout->addWidget(m_label);

    if (p_buttons & Button::Delete) {
        auto btn = createButton(Button::Delete, this);
        mainLayout->addWidget(btn);
    }
}

void LabelWithButtonsWidget::setLabel(const QString &p_label)
{
    m_label->setText(p_label);
}

void LabelWithButtonsWidget::paintEvent(QPaintEvent *p_event)
{
    Q_UNUSED(p_event);

    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

QToolButton *LabelWithButtonsWidget::createButton(Button p_button, QWidget *p_parent)
{
    auto btn = new QToolButton(p_parent);
    QAction *act = nullptr;
    switch (p_button) {
    case Button::Delete:
        act = new QAction(generateIcon(QStringLiteral("delete.svg")), tr("Delete"), p_parent);
        break;

    default:
        Q_ASSERT(false);
        break;
    }

    if (act) {
        act->setData(static_cast<int>(p_button));
        btn->setDefaultAction(act);
        connect(btn, &QToolButton::triggered,
                this, [this](QAction *p_act) {
                    emit triggered(static_cast<Button>(p_act->data().toInt()));
                });
    }
    return btn;
}

QIcon LabelWithButtonsWidget::generateIcon(const QString &p_name) const
{
    auto iconFile = VNoteX::getInst().getThemeMgr().getIconFile(p_name);
    return IconUtils::fetchIcon(iconFile);
}
