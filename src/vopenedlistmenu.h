#ifndef VOPENEDLISTMENU_H
#define VOPENEDLISTMENU_H

#include <QMenu>
#include <QMap>

class VEditWindow;
class VFile;
class QAction;
class QKeyEvent;
class QTimer;

class VOpenedListMenu : public QMenu
{
    Q_OBJECT
public:
    struct ItemInfo {
        VFile *file;
        int index;
    };

    explicit VOpenedListMenu(VEditWindow *p_editWin);

    bool isAccepted() const;

protected:
    void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;

signals:
    void fileTriggered(VFile *p_file);

private slots:
    void updateOpenedList();
    void handleItemTriggered(QAction *p_action);
    void cmdTimerTimeout();

private:
    QString generateDescription(const VFile *p_file) const;
    void addDigit(int p_digit);
    int getNumOfDigit(int p_num);
    void triggerItem(int p_seq);

    VEditWindow *m_editWin;

    // The number user pressed.
    int m_cmdNum;

    QTimer *m_cmdTimer;
    QMap<int, QAction*> m_seqActionMap;

    // Whether the menu is accepted or rejected.
    bool m_accepted;
};

inline bool VOpenedListMenu::isAccepted() const
{
    return m_accepted;
}
#endif // VOPENEDLISTMENU_H
