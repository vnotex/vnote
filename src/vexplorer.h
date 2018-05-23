#ifndef VEXPLORER_H
#define VEXPLORER_H

#include <QWidget>
#include <QVector>

#include "vexplorerentry.h"
#include "vconstants.h"

class QPushButton;
class QTreeView;
class QTreeWidgetItem;
class QShowEvent;
class QFocusEvent;
class QComboBox;
class VLineEdit;

class VExplorer : public QWidget
{
    Q_OBJECT
public:
    explicit VExplorer(QWidget *p_parent = nullptr);

protected:
    void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

    void focusInEvent(QFocusEvent *p_event) Q_DECL_OVERRIDE;

private slots:
    void handleEntryActivated(int p_idx);

    void handleContextMenuRequested(QPoint p_pos);

private:
    void setupUI();

    void init();

    void updateUI();

    void updateDirectoryComboBox();

    // Add an entry to m_entries with @p_dir being the path of the entry.
    // Return the index of the corresponding entry if succeed; otherwise,
    // return -1.
    int addEntry(const QString &p_dir);

    void setCurrentEntry(int p_index);

    bool checkIndex() const;

    void updateExplorerEntryIndexInConfig();

    void updateStarButton();

    void updateTree();

    void openFiles(const QStringList &p_files,
                   OpenFileMode p_mode,
                   bool p_forceMode = false);

    void resizeTreeToContents();

    void newFile();

    void newFolder();

    void renameFile(const QString &p_filePath);

    bool m_initialized;

    bool m_uiInitialized;

    QVector<VExplorerEntry> m_entries;

    int m_index;

    QPushButton *m_openBtn;
    QPushButton *m_openLocationBtn;
    QPushButton *m_starBtn;
    QComboBox *m_dirCB;
    VLineEdit *m_imgFolderEdit;
    QPushButton *m_newFileBtn;
    QPushButton *m_newDirBtn;
    QTreeView *m_tree;

    static const QString c_infoShortcutSequence;
};

inline bool VExplorer::checkIndex() const
{
    return m_index >= 0 && m_index < m_entries.size();
}

#endif // VEXPLORER_H
