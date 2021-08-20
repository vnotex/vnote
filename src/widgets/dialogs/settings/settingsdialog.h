#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "../dialog.h"

#include <functional>

class QTreeWidget;
class QStackedLayout;
class QLineEdit;
class QTreeWidgetItem;
class QScrollArea;

namespace vnotex
{
    class SettingsPage;

    class SettingsDialog : public Dialog
    {
        Q_OBJECT
    public:
        explicit SettingsDialog(QWidget *p_parent = nullptr);

    protected:
        void acceptedButtonClicked() Q_DECL_OVERRIDE;

        void resetButtonClicked() Q_DECL_OVERRIDE;

        void appliedButtonClicked() Q_DECL_OVERRIDE;

    private:
        void setupUI();

        void setupPageExplorer(QBoxLayout *p_layout, QWidget *p_parent);

        void setupPages();

        void setupPage(QTreeWidgetItem *p_item, SettingsPage *p_page);

        SettingsPage *itemPage(QTreeWidgetItem *p_item) const;

        void setChangesUnsaved(bool p_unsaved);

        bool savePages();

        // @p_func: return true to continue the iteration.
        void forEachPage(const std::function<bool(SettingsPage *)> &p_func);

        QTreeWidgetItem *addPage(SettingsPage *p_page);

        QTreeWidgetItem *addSubPage(SettingsPage *p_page, QTreeWidgetItem *p_parentItem);

        QLineEdit *m_searchEdit = nullptr;

        QTreeWidget *m_pageExplorer = nullptr;

        QScrollArea *m_scrollArea = nullptr;

        QStackedLayout *m_pageLayout = nullptr;

        bool m_changesUnsaved = false;

        bool m_ready = false;
    };
}

#endif // SETTINGSDIALOG_H
