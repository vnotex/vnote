#ifndef VUNIVERSALENTRY_H
#define VUNIVERSALENTRY_H

#include <QWidget>
#include <QRect>

class VLineEdit;
class QVBoxLayout;
class QHideEvent;
class QShowEvent;

class VUniversalEntry : public QWidget
{
    Q_OBJECT
public:
    explicit VUniversalEntry(QWidget *p_parent = nullptr);

    // Set the availabel width and height.
    void setAvailableRect(const QRect &p_rect);

signals:
    // Exit Universal Entry.
    void exited();

protected:
    void hideEvent(QHideEvent *p_event) Q_DECL_OVERRIDE;

    void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

private:
    void setupUI();

    VLineEdit *m_cmdEdit;

    QVBoxLayout *m_layout;

    // Rect availabel for the UE to use.
    QRect m_availableRect;

    int m_minimumWidth;
};

#endif // VUNIVERSALENTRY_H
