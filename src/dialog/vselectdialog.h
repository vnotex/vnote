#ifndef VSELECTDIALOG_H
#define VSELECTDIALOG_H

#include <QDialog>
#include <QMap>

class QPushButton;
class QVBoxLayout;
class QMouseEvent;

class VSelectDialog : public QDialog
{
    Q_OBJECT
public:
    VSelectDialog(const QString &p_title, QWidget *p_parent = 0);

    void addSelection(const QString &p_selectStr, int p_selectID);
    int getSelection() const;

private slots:
    void selectionChosen();

private:
    void setupUI(const QString &p_title);

    QVBoxLayout *m_mainLayout;
    int m_choice;
    QMap<QPushButton *, int> m_selections;
};

#endif // VSELECTDIALOG_H
