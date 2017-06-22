#ifndef VUPDATER_H
#define VUPDATER_H

#include <QDialog>
#include <QByteArray>

class QLabel;
class QDialogButtonBox;
class QTextBrowser;
class QProgressBar;
class QShowEvent;

class VUpdater : public QDialog
{
    Q_OBJECT
public:
    VUpdater(QWidget *p_parent = 0);

protected:
    void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

private slots:
    // Calling to Github api got responses.
    void parseResult(const QByteArray &p_data);

private:
    void setupUI();

    // Fetch the latest release info from Github.
    void checkUpdates();

    QLabel *m_versionLabel;
    QTextBrowser *m_descriptionTb;
    QDialogButtonBox *m_btnBox;

    // Progress label and bar.
    QLabel *m_proLabel;
    QProgressBar *m_proBar;
};

#endif // VUPDATER_H
