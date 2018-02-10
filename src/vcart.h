#ifndef VCART_H
#define VCART_H

#include <QWidget>
#include <QVector>

#include "vnavigationmode.h"

class QPushButton;
class QListWidget;
class QListWidgetItem;
class QLabel;
class QAction;
class QKeyEvent;
class QFocusEvent;

class VCart : public QWidget
{
    Q_OBJECT
public:
    explicit VCart(QWidget *p_parent = nullptr);

    void addFile(const QString &p_filePath);

    int count() const;

    QVector<QString> getFiles() const;

private slots:
    void handleContextMenuRequested(QPoint p_pos);

    void deleteSelectedItems();

    void openSelectedItems() const;

    void openItem(const QListWidgetItem *p_item) const;

    void locateCurrentItem();

private:
    void setupUI();

    void initActions();

    // Return index of item.
    int findFileInCart(const QString &p_file) const;

    void addItem(const QString &p_path);

    QString getFilePath(const QListWidgetItem *p_item) const;

    QPushButton *m_clearBtn;
    QLabel *m_numLabel;
    QListWidget *m_itemList;

    QAction *m_openAct;
    QAction *m_locateAct;
    QAction *m_deleteAct;
};

#endif // VCART_H
