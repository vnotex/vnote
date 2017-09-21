#ifndef VNOTEBOOKSELECTOR_H
#define VNOTEBOOKSELECTOR_H

#include <QComboBox>
#include <QVector>
#include <QString>
#include "vnavigationmode.h"

class VNotebook;
class VNote;
class VEditArea;
class QListWidget;
class QAction;
class QListWidgetItem;
class QLabel;

class VNotebookSelector : public QComboBox, public VNavigationMode
{
    Q_OBJECT
public:
    explicit VNotebookSelector(VNote *vnote, QWidget *p_parent = 0);
    void update();
    inline void setEditArea(VEditArea *p_editArea);
    // Select notebook @p_notebook.
    bool locateNotebook(const VNotebook *p_notebook);
    void showPopup() Q_DECL_OVERRIDE;

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
    bool newNotebook();

protected:
    bool eventFilter(QObject *watched, QEvent *event) Q_DECL_OVERRIDE;

private slots:
    void handleCurIndexChanged(int p_index);
    void handleItemActivated(int p_index);
    void requestPopupListContextMenu(QPoint p_pos);
    void deleteNotebook();
    void editNotebookInfo();

private:
    void initActions();
    void updateComboBox();

    // Return the index of @p_notebook in m_noteboks.
    int indexOfNotebook(const VNotebook *p_notebook);

    // If @p_import is true, we will use the existing config file.
    // If @p_imageFolder is empty, we will use the global one.
    // If @p_attachmentFolder is empty, we will use the global one.
    void createNotebook(const QString &p_name, const QString &p_path,
                        bool p_import, const QString &p_imageFolder,
                        const QString &p_attachmentFolder);

    void deleteNotebook(VNotebook *p_notebook, bool p_deleteFiles);
    void addNotebookItem(const QString &p_name);
    // @p_index is the index of m_notebooks, NOT of QComboBox.
    void removeNotebookItem(int p_index);
    // @p_index is the index of QComboBox.
    void updateComboBoxItem(int p_index, const QString &p_name);
    void insertAddNotebookItem();
    // @p_index is the index of m_notebooks.
    void setCurrentIndexNotebook(int p_index);
    int indexOfListItem(const QListWidgetItem *p_item);
    // @p_index is the idnex of QComboBox.
    inline VNotebook *getNotebookFromComboIndex(int p_index);
    void resizeListWidgetToContent();
    bool handlePopupKeyPress(QKeyEvent *p_event);

    VNote *m_vnote;
    QVector<VNotebook *> &m_notebooks;
    VEditArea *m_editArea;
    QListWidget *m_listWidget;
    int m_lastValidIndex;

    // Actions
    QAction *m_deleteNotebookAct;
    QAction *m_notebookInfoAct;
    QAction *m_openLocationAct;
    QAction *m_recycleBinAct;
    QAction *m_emptyRecycleBinAct;

    // We will add several special action item in the combobox. This is the start index
    // of the real notebook items related to m_notebooks.
    static const int c_notebookStartIdx;

    QLabel *m_naviLabel;
};

inline void VNotebookSelector::setEditArea(VEditArea *p_editArea)
{
    m_editArea = p_editArea;
}

inline VNotebook *VNotebookSelector::getNotebookFromComboIndex(int p_index)
{
    if (p_index < c_notebookStartIdx) {
        return NULL;
    }
    int nbIdx = p_index - c_notebookStartIdx;
    if (nbIdx >= m_notebooks.size()) {
        return NULL;
    }
    return m_notebooks[nbIdx];
}

#endif // VNOTEBOOKSELECTOR_H
