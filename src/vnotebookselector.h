#ifndef VNOTEBOOKSELECTOR_H
#define VNOTEBOOKSELECTOR_H

#include <QComboBox>
#include <QKeyEvent>
#include <QVector>
#include <QString>
#include "vnavigationmode.h"

class VNotebook;
class QListWidget;
class QListWidgetItem;
class QLabel;

class VNotebookSelector : public QComboBox, public VNavigationMode
{
    Q_OBJECT
public:
    explicit VNotebookSelector(QWidget *p_parent = 0);

    // Update Combox from m_notebooks.
    void update();

    // Select notebook @p_notebook.
    bool locateNotebook(const VNotebook *p_notebook);

    // Add notebook on popup if no notebooks currently.
    void showPopup() Q_DECL_OVERRIDE;

    VNotebook *currentNotebook() const;

    void restoreCurrentNotebook();

    // Implementations for VNavigationMode.
    void registerNavigation(QChar p_majorKey) Q_DECL_OVERRIDE;
    void showNavigation() Q_DECL_OVERRIDE;
    void hideNavigation() Q_DECL_OVERRIDE;
    bool handleKeyNavigation(int p_key, bool &p_succeed) Q_DECL_OVERRIDE;

signals:
    void curNotebookChanged(VNotebook *p_notebook);

    // Info of current notebook was changed.
    void notebookUpdated(const VNotebook *p_notebook);

    // Emit after creating a new notebook.
    void notebookCreated(const QString &p_name, bool p_import);

public slots:
    // Popup a dialog to prompt user to create a notebook.
    bool newNotebook();

protected:
    bool eventFilter(QObject *watched, QEvent *event) Q_DECL_OVERRIDE;

private slots:
    // Act to currentIndexChanged() signal if m_muted is false.
    void handleCurIndexChanged(int p_index);

    void popupListContextMenuRequested(QPoint p_pos);

    // Delete currently selected notebook.
    void deleteNotebook();

    // View and edit notebook information of selected notebook.
    void editNotebookInfo();

    // Sort notebooks.
    void sortItems();

private:
    // Update Combox from m_notebooks.
    void updateComboBox();

    // Return the item index of @p_notebook.
    int itemIndexOfNotebook(const VNotebook *p_notebook) const;

    // If @p_import is true, we will use the existing config file.
    // If @p_imageFolder is empty, we will use the global one.
    // If @p_attachmentFolder is empty, we will use the global one.
    void createNotebook(const QString &p_name,
                        const QString &p_path,
                        bool p_import,
                        const QString &p_imageFolder,
                        const QString &p_attachmentFolder);

    void deleteNotebook(VNotebook *p_notebook, bool p_deleteFiles);

    // Add an item corresponding to @p_notebook to combo box.
    void addNotebookItem(const VNotebook *p_notebook);

    void fillItem(QListWidgetItem *p_item, const VNotebook *p_notebook) const;

    // Insert "Add Notebook" item to combo box.
    void insertAddNotebookItem();

    // Set current item corresponding to @p_notebook.
    void setCurrentItemToNotebook(const VNotebook *p_notebook);

    // Get VNotebook from @p_itemIdx, the index of m_listWidget.
    VNotebook *getNotebook(int p_itemIdx) const;

    VNotebook *getNotebook(const QListWidgetItem *p_item) const;

    void resizeListWidgetToContent();

    bool handlePopupKeyPress(QKeyEvent *p_event);

    QVector<VNotebook *> &m_notebooks;

    QListWidget *m_listWidget;

    // Used to restore after clicking Add Notebook item.
    int m_lastValidIndex;

    // Whether it is muted from currentIndexChanged().
    bool m_muted;

    QLabel *m_naviLabel;
};

#endif // VNOTEBOOKSELECTOR_H
