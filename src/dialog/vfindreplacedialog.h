#ifndef VFINDREPLACEDIALOG_H
#define VFINDREPLACEDIALOG_H

#include <QWidget>

class QLineEdit;
class QPushButton;

class VFindReplaceDialog : public QWidget
{
    Q_OBJECT
public:
    explicit VFindReplaceDialog(QWidget *p_parent = 0);

signals:
    void dialogClosed();

protected:
    void showEvent(QShowEvent *event) Q_DECL_OVERRIDE;
    void keyPressEvent(QKeyEvent *event) Q_DECL_OVERRIDE;

public slots:
    void closeDialog();

private:
    void setupUI();
    QLineEdit *m_findEdit;
    QLineEdit *m_replaceEdit;
    QPushButton *m_findNextBtn;
    QPushButton *m_findPrevBtn;
    QPushButton *m_replaceBtn;
    QPushButton *m_replaceFindBtn;
    QPushButton *m_replaceAllBtn;
    QPushButton *m_advancedBtn;
    QPushButton *m_closeBtn;
};

#endif // VFINDREPLACEDIALOG_H
