#ifndef HISTORYPANEL_H
#define HISTORYPANEL_H

#include <QFrame>
#include <QDateTime>

class QListWidget;
class QListWidgetItem;

namespace vnotex
{
    class TitleBar;
    struct HistoryItemFull;

    class HistoryPanel : public QFrame
    {
        Q_OBJECT
    public:
        explicit HistoryPanel(QWidget *p_parent = nullptr);

    protected:
        void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

    private slots:
        void handleContextMenuRequested(const QPoint &p_pos);

        void openItem(const QListWidgetItem *p_item);

        void clearHistory();

    private:
        struct SeparatorData
        {
            QString m_text;

            QDateTime m_dateUtc;
        };

        void setupUI();

        void setupTitleBar(const QString &p_title, QWidget *p_parent = nullptr);

        void updateHistoryList();

        void updateHistoryListIfProper();

        void updateSeparators();

        void addItem(const HistoryItemFull &p_hisItem);

        QString getPath(const QListWidgetItem *p_item) const;

        bool isValidItem(const QListWidgetItem *p_item) const;

        TitleBar *m_titleBar = nullptr;

        QListWidget *m_historyList = nullptr;

        bool m_initialized = false;

        bool m_pendingUpdate = true;

        QVector<SeparatorData> m_separators;
    };
}

#endif // HISTORYPANEL_H
