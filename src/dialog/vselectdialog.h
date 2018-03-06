#ifndef VSELECTDIALOG_H
#define VSELECTDIALOG_H

#include <QDialog>
#include <QMap>

class QPushButton;
class QMouseEvent;
class QListWidget;
class QListWidgetItem;
class QShowEvent;
class QKeyEvent;

class VSelectDialog : public QDialog
{
    Q_OBJECT
public:
    VSelectDialog(const QString &p_title, QWidget *p_parent = 0);

    // @p_selectID should >= 0.
    void addSelection(const QString &p_selectStr, int p_selectID);

    int getSelection() const;

protected:
    void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

    void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;

private slots:
    void selectionChosen(QListWidgetItem *p_item);

private:
    void setupUI(const QString &p_title);

    void updateSize();

    int m_choice;

    QListWidget *m_list;
};

#endif // VSELECTDIALOG_H
