#ifndef VKEYBOARDLAYOUTMAPPINGDIALOG_H
#define VKEYBOARDLAYOUTMAPPINGDIALOG_H

#include <QDialog>


class QDialogButtonBox;
class QString;
class QTreeWidget;
class VLineEdit;
class QPushButton;
class QComboBox;

class VKeyboardLayoutMappingDialog : public QDialog
{
    Q_OBJECT
public:
    explicit VKeyboardLayoutMappingDialog(QWidget *p_parent = nullptr);

protected:
    bool eventFilter(QObject *p_obj, QEvent *p_event) Q_DECL_OVERRIDE;

private slots:
    void newMapping();

    void deleteCurrentMapping();

    // Return true if changes are saved.
    bool applyChanges();

private:
    void setupUI();

    void loadAvailableMappings();

    void loadMappingInfo(const QString &p_layout);

    void updateButtons();

    QString currentMapping() const;

    void setCurrentMapping(const QString &p_layout);

    QString getNewMappingName() const;

    bool listenKey(QKeyEvent *p_event);

    void cancelListeningKey();

    void setListeningKey(int p_idx);

    void setModified(bool p_modified);

    QComboBox *m_selectorCombo;
    QPushButton *m_addBtn;
    QPushButton *m_deleteBtn;
    VLineEdit *m_nameEdit;
    QTreeWidget *m_contentTree;
    QPushButton *m_applyBtn;

    bool m_mappingModified;

    // Index of the item in the tree which is listening key.
    // -1 for not listening.
    int m_listenIndex;
};

inline void VKeyboardLayoutMappingDialog::setModified(bool p_modified)
{
    m_mappingModified = p_modified;
    updateButtons();
}
#endif // VKEYBOARDLAYOUTMAPPINGDIALOG_H
