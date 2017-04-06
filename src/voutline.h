#ifndef VOUTLINE_H
#define VOUTLINE_H

#include <QTreeWidget>
#include <QVector>
#include <QMap>
#include <QChar>
#include "vtoc.h"
#include "vnavigationmode.h"

class QLabel;

class VOutline : public QTreeWidget, public VNavigationMode
{
    Q_OBJECT
public:
    VOutline(QWidget *parent = 0);

    // Implementations for VNavigationMode.
    void registerNavigation(QChar p_majorKey);
    void showNavigation();
    void hideNavigation();
    bool handleKeyNavigation(int p_key, bool &p_succeed);

signals:
    void outlineItemActivated(const VAnchor &anchor);

public slots:
    void updateOutline(const VToc &toc);
    void updateCurHeader(const VAnchor &anchor);

protected:
    void keyPressEvent(QKeyEvent *event) Q_DECL_OVERRIDE;

private slots:
    void handleCurItemChanged(QTreeWidgetItem *p_curItem, QTreeWidgetItem *p_preItem);

private:
    void updateTreeFromOutline(const VToc &toc);
    void updateTreeByLevel(const QVector<VHeader> &headers, int &index, QTreeWidgetItem *parent,
                           QTreeWidgetItem *last, int level);
    void expandTree();
    void selectAnchor(const QString &anchor);
    bool selectAnchorOne(QTreeWidgetItem *item, const QString &anchor);
    void selectLineNumber(int lineNumber);
    bool selectLineNumberOne(QTreeWidgetItem *item, int lineNumber);
    QList<QTreeWidgetItem *> getVisibleItems() const;
    QList<QTreeWidgetItem *> getVisibleChildItems(const QTreeWidgetItem *p_item) const;

    VToc outline;
    VAnchor curHeader;

    // Navigation Mode.
    // Map second key to QTreeWidgetItem.
    QMap<QChar, QTreeWidgetItem *> m_keyMap;
    QVector<QLabel *> m_naviLabels;
};

#endif // VOUTLINE_H
