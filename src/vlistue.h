#ifndef VLISTUE_H
#define VLISTUE_H

#include "iuniversalentry.h"

#include <QWidget>
#include <QIcon>
#include <functional>
#include <QSharedPointer>

#include "vsearchconfig.h"

class VListWidgetDoubleRows;
class QListWidgetItem;
class QLabel;
class VUETitleContentPanel;

class VListUE : public IUniversalEntry
{
    Q_OBJECT
public:
    enum ID
    {
        // List and search the history.
        History = 0
    };

    explicit VListUE(QObject *p_parent = nullptr);

    QString description(int p_id) const Q_DECL_OVERRIDE;

    QWidget *widget(int p_id) Q_DECL_OVERRIDE;

    void processCommand(int p_id, const QString &p_cmd) Q_DECL_OVERRIDE;

    void clear(int p_id) Q_DECL_OVERRIDE;

    void selectNextItem(int p_id, bool p_forward) Q_DECL_OVERRIDE;

    void activate(int p_id) Q_DECL_OVERRIDE;

    void sort(int p_id) Q_DECL_OVERRIDE;

protected:
    void init() Q_DECL_OVERRIDE;

private slots:
    void activateItem(QListWidgetItem *p_item);

private:
    void addResultItem(const QSharedPointer<VSearchResultItem> &p_item);

    const QSharedPointer<VSearchResultItem> &itemResultData(const QListWidgetItem *p_item) const;

    void listHistory(const QString &p_cmd);

    QVector<QSharedPointer<VSearchResultItem> > m_data;

    QIcon m_noteIcon;
    QIcon m_folderIcon;

    VListWidgetDoubleRows *m_listWidget;

    VUETitleContentPanel *m_panel;
};

#endif // VLISTUE_H
