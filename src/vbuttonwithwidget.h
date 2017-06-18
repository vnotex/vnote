#ifndef VBUTTONWITHWIDGET_H
#define VBUTTONWITHWIDGET_H

#include <QPushButton>
#include <QString>
#include <QIcon>

// A QPushButton with popup widget.
class VButtonWithWidget : public QPushButton
{
    Q_OBJECT
public:
    VButtonWithWidget(QWidget *p_parent = Q_NULLPTR);
    VButtonWithWidget(const QString &p_text, QWidget *p_parent = Q_NULLPTR);
    VButtonWithWidget(const QIcon &p_icon,
                      const QString &p_text,
                      QWidget *p_parent = Q_NULLPTR);
    ~VButtonWithWidget();

    // Set the widget which will transfer the ownership to VButtonWithWidget.
    void setPopupWidget(QWidget *p_widget);

    QWidget *getPopupWidget() const;

signals:
    // Emit when popup widget is about to show.
    void popupWidgetAboutToShow(QWidget *p_widget);

protected:
    bool eventFilter(QObject *p_obj, QEvent *p_event) Q_DECL_OVERRIDE;

private slots:
    // Show the popup widget.
    void showPopupWidget();

private:
    void init();

    QWidget *m_popupWidget;
};

#endif // VBUTTONWITHWIDGET_H
