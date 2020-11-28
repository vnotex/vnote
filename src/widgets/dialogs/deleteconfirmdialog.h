#ifndef DELETECONFIRMDIALOG_H
#define DELETECONFIRMDIALOG_H

#include "scrolldialog.h"

#include <QIcon>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QCheckBox;
class QScrollArea;

namespace vnotex
{
    class SelectionItemWidget;

    // Information about a deletion item needed to confirm.
    struct ConfirmItemInfo
    {
        ConfirmItemInfo()
        {
        }

        ConfirmItemInfo(const QString &p_name,
                        const QString &p_tip,
                        const QString &p_path,
                        void *p_data)
            : m_name(p_name),
              m_tip(p_tip),
              m_path(p_path),
              m_data(p_data)
        {
        }

        ConfirmItemInfo(const QIcon &p_icon,
                        const QString &p_name,
                        const QString &p_tip,
                        const QString &p_path,
                        void *p_data)
            : m_icon(p_icon),
              m_name(p_name),
              m_tip(p_tip),
              m_path(p_path),
              m_data(p_data)
        {
        }

        QIcon m_icon;
        QString m_name;
        QString m_tip;
        QString m_path;
        void *m_data = nullptr;
    };


    class DeleteConfirmDialog : public ScrollDialog
    {
        Q_OBJECT
    public:
        enum Flag {
            None = 0,
            AskAgain = 0x1,
            Preview = 0x2
        };
        Q_DECLARE_FLAGS(Flags, Flag)

        DeleteConfirmDialog(const QString &p_title,
                           const QString &p_text,
                           const QString &p_info,
                           const QVector<ConfirmItemInfo> &p_items,
                           DeleteConfirmDialog::Flags p_flags,
                           bool p_noAskChecked,
                           QWidget *p_parent = nullptr);

        QVector<ConfirmItemInfo> getConfirmedItems() const;

        bool isNoAskChecked() const;

    private slots:
        void currentFileChanged(int p_row);

        void updateCountLabel();

    private:
        void setupUI(const QString &p_title,
                     const QString &p_text,
                     const QString &p_info,
                     DeleteConfirmDialog::Flags p_flags,
                     bool p_noAskChecked);

        void updateItemsList();

        SelectionItemWidget *getItemWidget(QListWidgetItem *p_item) const;

        QVector<ConfirmItemInfo> m_items;

        QLabel *m_countLabel = nullptr;
        QListWidget *m_listWidget = nullptr;

        QLabel *m_previewer = nullptr;

        QScrollArea *m_previewArea = nullptr;

        QCheckBox *m_noAskCB = nullptr;
    };

    Q_DECLARE_OPERATORS_FOR_FLAGS(DeleteConfirmDialog::Flags)
} // ns vnotex

#endif // DELETECONFIRMDIALOG_H
