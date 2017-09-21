#ifndef VBUTTONWITHWIDGET_H
#define VBUTTONWITHWIDGET_H

#include <QPushButton>
#include <QString>
#include <QIcon>
#include <QWidgetAction>

class VButtonWidgetAction : public QWidgetAction
{
    Q_OBJECT
public:
    VButtonWidgetAction(QWidget *p_widget, QWidget *p_parent)
        : QWidgetAction(p_parent), m_widget(p_widget)
    {
    }

    QWidget *createWidget(QWidget *p_parent)
    {
        m_widget->setParent(p_parent);
        return m_widget;
    }

private:
    QWidget *m_widget;
};

// A QPushButton with popup widget.
class VButtonWithWidget : public QPushButton
{
    Q_OBJECT
public:
    VButtonWithWidget(QWidget *p_widget,
                      QWidget *p_parent = Q_NULLPTR);

    VButtonWithWidget(const QString &p_text,
                      QWidget *p_widget,
                      QWidget *p_parent = Q_NULLPTR);

    VButtonWithWidget(const QIcon &p_icon,
                      const QString &p_text,
                      QWidget *p_widget,
                      QWidget *p_parent = Q_NULLPTR);

    QWidget *getPopupWidget() const;

    // Show the popup widget.
    void showPopupWidget();

signals:
    // Emit when popup widget is about to show.
    void popupWidgetAboutToShow(QWidget *p_widget);

private:
    void init();

    QWidget *m_popupWidget;
};

#endif // VBUTTONWITHWIDGET_H
