#ifndef VATTACHMENTLIST_H
#define VATTACHMENTLIST_H

#include <QWidget>
#include <QVector>
#include "vnotefile.h"

class QPushButton;
class QListWidget;
class QListWidgetItem;
class QLabel;
class VNoteFile;
class QAction;

class VAttachmentList : public QWidget
{
    Q_OBJECT
public:
    explicit VAttachmentList(QWidget *p_parent = 0);

    void setFile(VNoteFile *p_file);

private slots:
    void addAttachment();

    void handleContextMenuRequested(QPoint p_pos);

    void handleItemActivated(QListWidgetItem *p_item);

    void deleteSelectedItems();

    void sortItems();

    void handleListItemCommitData(QWidget *p_itemEdit);

private:
    void setupUI();

    void initActions();

    // Update attachment info of m_file.
    void updateContent();

    void fillAttachmentList(const QVector<VAttachment> &p_attachments);

    QPushButton *m_addBtn;
    QPushButton *m_clearBtn;
    QPushButton *m_locateBtn;
    QLabel *m_numLabel;

    QListWidget *m_attachmentList;

    QAction *m_openAct;
    QAction *m_deleteAct;
    QAction *m_sortAct;

    VNoteFile *m_file;
};

#endif // VATTACHMENTLIST_H
