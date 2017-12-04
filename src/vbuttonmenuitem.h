#ifndef VBUTTONMENUITEM_H
#define VBUTTONMENUITEM_H

#include <QPushButton>

class QAction;


class VButtonMenuItem : public QPushButton
{
    Q_OBJECT
public:
    explicit VButtonMenuItem(QAction *p_action, QWidget *p_parent = nullptr);

    VButtonMenuItem(QAction *p_action, const QString &p_text, QWidget *p_parent = nullptr);

private:
    void init();

    QAction *m_action;
};

#endif // VBUTTONMENUITEM_H
