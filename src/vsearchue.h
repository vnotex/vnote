#ifndef VSEARCHUE_H
#define VSEARCHUE_H


#include "iuniversalentry.h"

#include <QIcon>

#include "vsearchconfig.h"

class VListWidgetDoubleRows;
class QListWidgetItem;


// Universal Entry to list and search all the notebooks.
class VSearchUE : public IUniversalEntry
{
    Q_OBJECT
public:
    enum ID
    {
        // List and search the name of all notebooks.
        Name_Notebook_AllNotebook = 0,

        // Search the name of the folder/note in all the notebooks.
        Name_FolderNote_AllNotebook
    };

    explicit VSearchUE(QObject *p_parent = nullptr);

    QString description(int p_id) const Q_DECL_OVERRIDE;

    QWidget *widget(int p_id) Q_DECL_OVERRIDE;

    void processCommand(int p_id, const QString &p_cmd) Q_DECL_OVERRIDE;

    void clear(int p_id) Q_DECL_OVERRIDE;

    void entryHidden(int p_id) Q_DECL_OVERRIDE;

    void selectNextItem(int p_id, bool p_forward) Q_DECL_OVERRIDE;

    void activate(int p_id) Q_DECL_OVERRIDE;

    void askToStop(int p_id) Q_DECL_OVERRIDE;

protected:
    void init() Q_DECL_OVERRIDE;

private slots:
    void handleSearchItemAdded(const QSharedPointer<VSearchResultItem> &p_item);

    void handleSearchFinished(const QSharedPointer<VSearchResult> &p_result);

private:
    void searchNameOfAllNotebooks(const QString &p_cmd);

    void searchNameOfFolderNoteInAllNotebooks(const QString &p_cmd);

    // Stop the search synchronously.
    void stopSearch();

    void appendItemToList(const QSharedPointer<VSearchResultItem> &p_item);

    void activateItem(const QListWidgetItem *p_item);

    const QSharedPointer<VSearchResultItem> &itemResultData(const QListWidgetItem *p_item) const;

    VSearch *m_search;

    bool m_inSearch;

    // Current instance ID.
    int m_id;

    QVector<QSharedPointer<VSearchResultItem> > m_data;

    QIcon m_noteIcon;
    QIcon m_folderIcon;
    QIcon m_notebookIcon;

    VListWidgetDoubleRows *m_listWidget;
};

#endif // VSEARCHUE_H
