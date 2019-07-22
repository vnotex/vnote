#ifndef VTIPSDIALOG_H
#define VTIPSDIALOG_H

#include <functional>

#include <QDialog>

class QDialogButtonBox;
class QPushButton;
class QShowEvent;
class QTextBrowser;

typedef std::function<void()> TipsDialogFunc;

class VTipsDialog : public QDialog
{
    Q_OBJECT
public:
    VTipsDialog(const QString &p_tipFile,
                const QString &p_actionText = QString(),
                TipsDialogFunc p_action = nullptr,
                QWidget *p_parent = nullptr);

protected:
    void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

private:
    void setupUI(const QString &p_actionText);

    void readFile(const QString &p_tipFile);

    QTextBrowser *m_viewer;

    QDialogButtonBox *m_btnBox;

    QPushButton *m_actionBtn;

    QPushButton *m_okBtn;

    TipsDialogFunc m_action;
};

#endif // VTIPSDIALOG_H
