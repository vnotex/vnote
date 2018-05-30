#ifndef VHISTORYLIST_H
#define VHISTORYLIST_H

#include <QWidget>
#include <QLinkedList>

#include "vhistoryentry.h"
#include "vnavigationmode.h"

class QPushButton;
class VListWidget;
class QListWidgetItem;
class QShowEvent;
class QFocusEvent;

class VHistoryList : public QWidget, public VNavigationMode
{
    Q_OBJECT
public:
    explicit VHistoryList(QWidget *p_parent = nullptr);

    void pinFiles(const QStringList &p_files);

    void pinFolder(const QString &p_folder);

    const QLinkedList<VHistoryEntry> &getHistoryEntries() const;

    // Implementations for VNavigationMode.
    void showNavigation() Q_DECL_OVERRIDE;
    bool handleKeyNavigation(int p_key, bool &p_succeed) Q_DECL_OVERRIDE;

public slots:
    void addFile(const QString &p_filePath);

protected:
    void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

    void focusInEvent(QFocusEvent *p_event) Q_DECL_OVERRIDE;

private slots:
    void handleContextMenuRequested(QPoint p_pos);

    void openSelectedItems() const;

    void openItem(const QListWidgetItem *p_item) const;

    void pinSelectedItems();

    void unpinSelectedItems();

    void locateCurrentItem();

    // Add selected files to Cart.
    void addFileToCart() const;

private:
    void setupUI();

    // Read data from config file.
    void init();

    void updateList();

    QLinkedList<VHistoryEntry>::iterator findFileInHistory(const QString &p_file);

    QString getFilePath(const QListWidgetItem *p_item) const;

    VHistoryEntry *getHistoryEntry(const QListWidgetItem *p_item) const;

    bool isFolder(const QListWidgetItem *p_item) const;

    // @p_isPinned won't change it if an item is pinned already.
    void addFilesInternal(const QStringList &p_files, bool p_isPinned);

    void unpinFiles(const QStringList &p_files);

    void checkHistorySize();

    QPushButton *m_clearBtn;
    VListWidget *m_itemList;

    // Whether data is loaded.
    bool m_initialized;

    bool m_uiInitialized;

    // New files are appended to the end.
    QLinkedList<VHistoryEntry> m_histories;

    // Whether we need to update the list.
    bool m_updatePending;

    QDate m_currentDate;
};

inline const QLinkedList<VHistoryEntry> &VHistoryList::getHistoryEntries() const
{
    const_cast<VHistoryList *>(this)->init();

    return m_histories;
}
#endif // VHISTORYLIST_H
