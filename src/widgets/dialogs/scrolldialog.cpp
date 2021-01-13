#include "scrolldialog.h"

#include <QScrollArea>
#include <QVBoxLayout>
#include <QDebug>
#include <QApplication>
#include <QDesktopWidget>
#include <QScrollBar>
#include <QTimer>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QCoreApplication>

#include <utils/widgetutils.h>

#include <widgets/propertydefs.h>

using namespace vnotex;

ScrollDialog::ScrollDialog(QWidget *p_parent, Qt::WindowFlags p_flags)
    : Dialog(p_parent, p_flags)
{
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_layout->addWidget(m_scrollArea);
}

void ScrollDialog::setCentralWidget(QWidget *p_widget)
{
    Q_ASSERT(!m_centralWidget && p_widget);
    m_centralWidget = p_widget;
    m_centralWidget->setProperty(PropertyDefs::s_dialogCentralWidget, true);
    m_scrollArea->setWidget(p_widget);
}

void ScrollDialog::addBottomWidget(QWidget *p_widget)
{
    m_layout->insertWidget(m_layout->indexOf(m_scrollArea) + 1, p_widget);
}

void ScrollDialog::showEvent(QShowEvent *p_event)
{
    QDialog::showEvent(p_event);

    resizeToHideScrollBarLater(false, true);
}

void ScrollDialog::resizeToHideScrollBarLater(bool p_vertical, bool p_horizontal)
{
    WidgetUtils::resizeToHideScrollBarLater(m_scrollArea, p_vertical, p_horizontal);
}
