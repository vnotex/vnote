#ifndef VSEARCHUE_H
#define VSEARCHUE_H

#include "iuniversalentry.h"

#include <QIcon>

#include "vsearchconfig.h"

class VListWidgetDoubleRows;
class QListWidgetItem;
class VTreeWidget;
class QTreeWidgetItem;


// Universal Entry using VSearch.
class VSearchUE : public IUniversalEntry
{
    Q_OBJECT
public:
    enum ID
    {
        // List and search the name of all notebooks.
        Name_Notebook_AllNotebook = 0,

        // Search the name of the folder/note in all the notebooks.
        Name_FolderNote_AllNotebook,

        // Search content of the note in all the notebooks.
        Content_Note_AllNotebook,

        // Search tag of the note in all the notebooks.
        Tag_Note_AllNotebook,

        // Search the name of the folder/note in current notebook.
        Name_FolderNote_CurrentNotebook,

        // Search content of the note in current notebook.
        Content_Note_CurrentNotebook,

        // Search tag of the note in current notebook.
        Tag_Note_CurrentNotebook,

        // Search the name of the folder/note in current folder.
        Name_FolderNote_CurrentFolder,

        // Search content of the note in current folder.
        Content_Note_CurrentFolder,

        // Search the tag of the note in current folder.
        Tag_Note_CurrentFolder,

        // List and search the name of opened notes in buffer.
        Name_Note_Buffer,

        // Search content of opened notes in buffer.
        Content_Note_Buffer,

        // Search outline of opened notes in buffer.
        Outline_Note_Buffer,

        // Search path of folder/note in all the notebooks.
        Path_FolderNote_AllNotebook,

        // Search path of folder/note in current notebook.
        Path_FolderNote_CurrentNotebook,

        // Search content of the note in Explorer root directory.
        Content_Note_ExplorerDirectory
    };

    explicit VSearchUE(QObject *p_parent = nullptr);

    QString description(int p_id) const Q_DECL_OVERRIDE;

    QWidget *widget(int p_id) Q_DECL_OVERRIDE;

    void processCommand(int p_id, const QString &p_cmd) Q_DECL_OVERRIDE;

    void clear(int p_id) Q_DECL_OVERRIDE;

    void selectNextItem(int p_id, bool p_forward) Q_DECL_OVERRIDE;

    void selectParentItem(int p_id) Q_DECL_OVERRIDE;

    void activate(int p_id) Q_DECL_OVERRIDE;

    void askToStop(int p_id) Q_DECL_OVERRIDE;

    void toggleItemExpanded(int p_id) Q_DECL_OVERRIDE;

    void sort(int p_id) Q_DECL_OVERRIDE;

    QString currentItemFolder(int p_id) Q_DECL_OVERRIDE;

    static void activateItem(const QSharedPointer<VSearchResultItem> &p_item);

protected:
    void init() Q_DECL_OVERRIDE;

private slots:
    void handleSearchItemAdded(const QSharedPointer<VSearchResultItem> &p_item);

    void handleSearchItemsAdded(const QList<QSharedPointer<VSearchResultItem> > &p_items);

    void handleSearchFinished(const QSharedPointer<VSearchResult> &p_result);

    void activateItem(QListWidgetItem *p_item);

    void activateItem(QTreeWidgetItem *p_item, int p_col);

private:
    void searchNameOfAllNotebooks(const QString &p_cmd);

    void searchNameOfFolderNoteInAllNotebooks(const QString &p_cmd);

    void searchTagOfNoteInAllNotebooks(const QString &p_cmd);

    void searchTagOfNoteInCurrentNotebook(const QString &p_cmd);

    void searchTagOfNoteInCurrentFolder(const QString &p_cmd);

    void searchNameOfFolderNoteInCurrentNotebook(const QString &p_cmd);

    void searchNameOfFolderNoteInCurrentFolder(const QString &p_cmd);

    void searchContentOfNoteInAllNotebooks(const QString &p_cmd);

    void searchContentOfNoteInCurrentNotebook(const QString &p_cmd);

    void searchContentOfNoteInCurrentFolder(const QString &p_cmd);

    void searchContentOfNoteInExplorerDirectory(const QString &p_cmd);

    void searchNameOfBuffer(const QString &p_cmd);

    void searchContentOfBuffer(const QString &p_cmd);

    void searchOutlineOfBuffer(const QString &p_cmd);

    void searchPathOfFolderNoteInAllNotebooks(const QString &p_cmd);

    void searchPathOfFolderNoteInCurrentNotebook(const QString &p_cmd);

    // Stop the search synchronously.
    void stopSearch();

    void appendItemToList(const QSharedPointer<VSearchResultItem> &p_item);

    void appendItemToTree(const QSharedPointer<VSearchResultItem> &p_item);

    const QSharedPointer<VSearchResultItem> &itemResultData(const QListWidgetItem *p_item) const;

    const QSharedPointer<VSearchResultItem> &itemResultData(const QTreeWidgetItem *p_item) const;

    // Update geometry of widget.
    void updateWidget();

    VSearch *m_search;

    bool m_inSearch;

    // Current instance ID.
    int m_id;

    QVector<QSharedPointer<VSearchResultItem> > m_data;

    QIcon m_noteIcon;
    QIcon m_folderIcon;
    QIcon m_notebookIcon;

    VListWidgetDoubleRows *m_listWidget;

    VTreeWidget *m_treeWidget;
};

#endif // VSEARCHUE_H
