#ifndef VTAGEXPLORER_H
#define VTAGEXPLORER_H

#include <QWidget>
#include <QIcon>

#include "vsearchconfig.h"

class QLabel;
class VListWidget;
class QListWidgetItem;
class QSplitter;
class VNotebook;
class VSearch;

class VTagExplorer : public QWidget
{
    Q_OBJECT
public:
    explicit VTagExplorer(QWidget *p_parent = nullptr);

    void clear();

    void setNotebook(VNotebook *p_notebook);

    void saveStateAndGeometry();

    void registerNavigationTarget();

protected:
    void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

    void focusInEvent(QFocusEvent *p_event) Q_DECL_OVERRIDE;

private slots:
    void handleSearchItemAdded(const QSharedPointer<VSearchResultItem> &p_item);

    void handleSearchItemsAdded(const QList<QSharedPointer<VSearchResultItem> > &p_items);

    void handleSearchFinished(const QSharedPointer<VSearchResult> &p_result);

    void openFileItem(QListWidgetItem *p_item) const;

    void openSelectedFileItems() const;

    void locateCurrentFileItem() const;

    void addFileToCart() const;

    void pinFileToHistory() const;

    void handleFileListContextMenuRequested(QPoint p_pos);

private:
    void setupUI();

    void updateNotebookLabel();

    void updateTagLabel(const QString &p_tag);

    bool tagListObsolete(const QStringList &p_tags) const;

    void updateTagList(const QStringList &p_tags);

    void updateContent();

    // Return ture if succeeded.
    bool activateTag(const QString &p_tag);

    void addTagItem(const QString &p_tag);

    void restoreStateAndGeometry();

    VSearch *getVSearch() const;

    void initVSearch();

    void appendItemToFileList(const QSharedPointer<VSearchResultItem> &p_item);

    QString getFilePath(const QListWidgetItem *p_item) const;

    void promptToRemoveEmptyTag(const QString &p_tag);

    void setupFileListSplitOut(bool p_enabled);

    bool m_uiInitialized;

    QLabel *m_notebookLabel;

    QLabel *m_tagLabel;

    QPushButton *m_splitBtn;

    VListWidget *m_tagList;

    VListWidget *m_fileList;

    QSplitter *m_splitter;

    VNotebook *m_notebook;

    bool m_notebookChanged;

    QIcon m_noteIcon;

    VSearch *m_search;
};

inline VSearch *VTagExplorer::getVSearch() const
{
    if (m_search) {
        return m_search;
    }

    const_cast<VTagExplorer *>(this)->initVSearch();
    return m_search;
}

#endif // VTAGEXPLORER_H
