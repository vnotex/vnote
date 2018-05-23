#ifndef VCART_H
#define VCART_H

#include <QWidget>
#include <QVector>

#include "vnavigationmode.h"

class QPushButton;
class VListWidget;
class QListWidgetItem;
class QLabel;

class VCart : public QWidget, public VNavigationMode
{
    Q_OBJECT
public:
    explicit VCart(QWidget *p_parent = nullptr);

    void addFile(const QString &p_filePath);

    int count() const;

    QVector<QString> getFiles() const;

    // Implementations for VNavigationMode.
    void showNavigation() Q_DECL_OVERRIDE;
    bool handleKeyNavigation(int p_key, bool &p_succeed) Q_DECL_OVERRIDE;

protected:
    void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

    void focusInEvent(QFocusEvent *p_event) Q_DECL_OVERRIDE;

private slots:
    void handleContextMenuRequested(QPoint p_pos);

    void deleteSelectedItems();

    void openSelectedItems() const;

    void openItem(const QListWidgetItem *p_item) const;

    void locateCurrentItem();

    void sortItems();

private:
    void setupUI();

    void init();

    bool m_initialized;

    bool m_uiInitialized;

    // Return index of item.
    int findFileInCart(const QString &p_file) const;

    void addItem(const QString &p_path);

    QString getFilePath(const QListWidgetItem *p_item) const;

    void updateNumberLabel() const;

    QPushButton *m_clearBtn;
    QLabel *m_numLabel;
    VListWidget *m_itemList;
};

#endif // VCART_H
