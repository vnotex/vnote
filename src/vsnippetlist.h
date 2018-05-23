#ifndef VSNIPPETLIST_H
#define VSNIPPETLIST_H

#include <QWidget>
#include <QVector>
#include <QPoint>

#include "vsnippet.h"
#include "vnavigationmode.h"

class QPushButton;
class VListWidget;
class QListWidgetItem;
class QLabel;
class QKeyEvent;
class QFocusEvent;


class VSnippetList : public QWidget, public VNavigationMode
{
    Q_OBJECT
public:
    explicit VSnippetList(QWidget *p_parent = nullptr);

    const QVector<VSnippet> &getSnippets() const;

    const VSnippet *getSnippet(const QString &p_name) const;

    // Implementations for VNavigationMode.
    void showNavigation() Q_DECL_OVERRIDE;
    bool handleKeyNavigation(int p_key, bool &p_succeed) Q_DECL_OVERRIDE;

protected:
    void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

    void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;

    void focusInEvent(QFocusEvent *p_event) Q_DECL_OVERRIDE;

private slots:
    void newSnippet();

    void handleContextMenuRequested(QPoint p_pos);

    void handleItemActivated(QListWidgetItem *p_item);

    void snippetInfo();

    void deleteSelectedItems();

    void sortItems();

private:
    void setupUI();

    void init();

    void initShortcuts();

    void makeSureFolderExist() const;

    // Update list of snippets according to m_snippets.
    void updateContent();

    // Add @p_snippet.
    bool addSnippet(const VSnippet &p_snippet, QString *p_errMsg = nullptr);

    // Write m_snippets to config file.
    bool writeSnippetsToConfig() const;

    // Read from config file to m_snippets.
    bool readSnippetsFromConfig();

    // Get the snippet index in m_snippets of @p_item.
    int getSnippetIndex(QListWidgetItem *p_item) const;

    VSnippet *getSnippet(QListWidgetItem *p_item);

    // Write the content of @p_snippet to file.
    bool writeSnippetFile(const VSnippet &p_snippet, QString *p_errMsg = nullptr);

    QString getSnippetFilePath(const VSnippet &p_snippet) const;

    // Sort m_snippets according to @p_sortedIdx.
    bool sortSnippets(const QVector<int> &p_sortedIdx, QString *p_errMsg = nullptr);

    bool deleteSnippets(const QList<QString> &p_snippets, QString *p_errMsg = nullptr);

    bool deleteSnippetFile(const VSnippet &p_snippet, QString *p_errMsg = nullptr);

    void updateNumberLabel() const;

    bool m_initialized;

    bool m_uiInitialized;

    QPushButton *m_addBtn;
    QPushButton *m_locateBtn;
    QLabel *m_numLabel;
    VListWidget *m_snippetList;

    QVector<VSnippet> m_snippets;

    static const QString c_infoShortcutSequence;
};

inline const QVector<VSnippet> &VSnippetList::getSnippets() const
{
    const_cast<VSnippetList *>(this)->init();

    return m_snippets;
}

inline const VSnippet *VSnippetList::getSnippet(const QString &p_name) const
{
    const_cast<VSnippetList *>(this)->init();

    for (auto const & snip : m_snippets) {
        if (snip.getName() == p_name) {
            return &snip;
        }
    }

    return NULL;
}
#endif // VSNIPPETLIST_H
