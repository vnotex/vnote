#ifndef SNIPPETPANEL_H
#define SNIPPETPANEL_H

#include <QFrame>

class QListWidget;
class QListWidgetItem;

namespace vnotex
{
    class TitleBar;

    class SnippetPanel : public QFrame
    {
        Q_OBJECT
    public:
        explicit SnippetPanel(QWidget *p_parent = nullptr);

    signals:
        void applySnippetRequested(const QString &p_name);

    protected:
        void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

    private slots:
        void newSnippet();

        void handleContextMenuRequested(QPoint p_pos);

        void removeSelectedSnippets();

        void applySnippet(const QListWidgetItem *p_item);

    private:
        void setupUI();

        void setupTitleBar(const QString &p_title, QWidget *p_parent = nullptr);

        void updateItemsCountLabel();

        void updateSnippetList();

        QString getSnippetName(const QListWidgetItem *p_item);

        TitleBar *m_titleBar = nullptr;

        QListWidget *m_snippetList = nullptr;

        bool m_listInitialized = false;
    };
}

#endif // SNIPPETPANEL_H
