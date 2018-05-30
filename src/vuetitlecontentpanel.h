#ifndef VUETITLECONTENTPANEL_H
#define VUETITLECONTENTPANEL_H

#include <QWidget>

class QLabel;

class VUETitleContentPanel : public QWidget
{
    Q_OBJECT
public:
    explicit VUETitleContentPanel(QWidget *p_contentWidget,
                                  QWidget *p_parent = nullptr);

    void setTitle(const QString &p_title);

    void clearTitle();

private:
    QLabel *m_titleLabel;
};

#endif // VUETITLECONTENTPANEL_H
