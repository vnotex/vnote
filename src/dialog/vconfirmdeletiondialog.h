#ifndef VCONFIRMDELETIONDIALOG_H
#define VCONFIRMDELETIONDIALOG_H

#include <QDialog>
#include <QVector>

class QLabel;
class QPushButton;
class QDialogButtonBox;
class QListWidget;
class QListWidgetItem;
class ConfirmItemWidget;
class QCheckBox;

// Information about a deletion item needed to confirm.
struct ConfirmItemInfo
{
    ConfirmItemInfo()
        : m_data(NULL)
    {
    }

    ConfirmItemInfo(const QString &p_name,
                    const QString &p_tip,
                    const QString &p_path,
                    void *p_data)
        : m_name(p_name), m_tip(p_tip), m_path(p_path), m_data(p_data)
    {
    }

    QString m_name;
    QString m_tip;
    QString m_path;
    void *m_data;
};

class ConfirmItemWidget : public QWidget
{
    Q_OBJECT
public:
    ConfirmItemWidget(bool p_checked,
                      const QString &p_file,
                      const QString &p_tip,
                      int p_index,
                      QWidget *p_parent = NULL);

    bool isChecked() const;

    int getIndex() const;

signals:
    // Emit when item's check state changed.
    void checkStateChanged(int p_state);

private:
    void setupUI();

    QCheckBox *m_checkBox;
    QLabel *m_fileLabel;

    int m_index;
};

class VConfirmDeletionDialog : public QDialog
{
    Q_OBJECT
public:
    VConfirmDeletionDialog(const QString &p_title,
                           const QString &p_text,
                           const QString &p_info,
                           const QVector<ConfirmItemInfo> &p_items,
                           bool p_enableAskAgain,
                           bool p_askAgainEnabled,
                           bool p_enablePreview,
                           QWidget *p_parent = 0);

    QVector<ConfirmItemInfo> getConfirmedItems() const;

    bool getAskAgainEnabled() const;

private slots:
    void currentFileChanged(int p_row);

    void updateCountLabel();

private:
    void setupUI(const QString &p_title,
                 const QString &p_text,
                 const QString &p_info);

    void initItems();

    ConfirmItemWidget *getItemWidget(QListWidgetItem *p_item) const;

    QLabel *m_countLabel;
    QListWidget *m_listWidget;
    QLabel *m_previewer;
    QDialogButtonBox *m_btnBox;
    QCheckBox *m_askAgainCB;

    bool m_enableAskAgain;
    // Init value if m_enableAskAgain is true.
    bool m_askAgainEnabled;

    bool m_enablePreview;

    QVector<ConfirmItemInfo> m_items;
};

#endif // VCONFIRMDELETIONDIALOG_H
