#ifndef VATTACHMENTLIST_H
#define VATTACHMENTLIST_H

#include <QWidget>
#include <QVector>
#include <QStringList>
#include "vnotefile.h"
#include "vbuttonwithwidget.h"
#include "lineeditdelegate.h"

class QPushButton;
class QListWidget;
class QListWidgetItem;
class QLabel;
class VNoteFile;


class VAttachmentList : public QWidget, public VButtonPopupWidget
{
    Q_OBJECT
public:
    explicit VAttachmentList(QWidget *p_parent = 0);

    // Need to call updateContent() to update the list.
    void setFile(VNoteFile *p_file);

    bool isAcceptDrops() const Q_DECL_OVERRIDE;

    bool handleDragEnterEvent(QDragEnterEvent *p_event) Q_DECL_OVERRIDE;

    bool handleDropEvent(QDropEvent *p_event) Q_DECL_OVERRIDE;

    void handleAboutToShow() Q_DECL_OVERRIDE;

protected:
    void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;

    void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

private slots:
    void addAttachment();

    void handleContextMenuRequested(QPoint p_pos);

    void handleItemActivated(QListWidgetItem *p_item);

    void deleteSelectedItems();

    void sortItems();

    void handleListItemCommitData(QWidget *p_itemEdit);

    void attachmentInfo();

private:
    void setupUI();

    void init();

    void fillAttachmentList(const QVector<VAttachment> &p_attachments);

    void addAttachments(const QStringList &p_files);

    // Update the state of VButtonWithWidget.
    void updateButtonState() const;

    // Check if there are attachments that do not exist in disk.
    void checkAttachments();

    // Update attachment info of m_file.
    void updateContent();

    bool m_initialized;

    QPushButton *m_addBtn;
    QPushButton *m_clearBtn;
    QPushButton *m_locateBtn;
    QLabel *m_numLabel;

    LineEditDelegate m_listDelegate;
    QListWidget *m_attachmentList;

    VNoteFile *m_file;

    static const QString c_infoShortcutSequence;
};

#endif // VATTACHMENTLIST_H
