#include <QDebug>
#include <QVector>
#include <QString>
#include <QJsonObject>
#include "voutline.h"
#include "vtoc.h"

VOutline::VOutline(QWidget *parent)
    : QTreeWidget(parent)
{
    setColumnCount(1);
    setHeaderHidden(true);
    setSelectionMode(QAbstractItemView::SingleSelection);

    connect(this, &VOutline::itemClicked,
            this, &VOutline::handleItemClicked);
}

void VOutline::updateOutline(const VToc &toc)
{
    outline = toc;
    updateTreeFromOutline(outline);
    expandTree();
}

void VOutline::updateTreeFromOutline(const VToc &toc)
{
    clear();

    if (!toc.valid) {
        return;
    }
    const QVector<VHeader> &headers = toc.headers;
    int idx = 0;
    updateTreeByLevel(headers, idx, NULL, NULL, 1);
}

void VOutline::updateTreeByLevel(const QVector<VHeader> &headers, int &index,
                                 QTreeWidgetItem *parent, QTreeWidgetItem *last, int level)
{
    while (index < headers.size()) {
        const VHeader &header = headers[index];
        QTreeWidgetItem *item;
        if (header.level == level) {
            if (parent) {
                item = new QTreeWidgetItem(parent);
            } else {
                item = new QTreeWidgetItem(this);
            }
            QJsonObject itemJson;
            itemJson["anchor"] = header.anchor;
            itemJson["line_number"] = header.lineNumber;
            item->setData(0, Qt::UserRole, itemJson);
            item->setText(0, header.name);

            last = item;
            ++index;
        } else if (header.level < level) {
            return;
        } else {
            updateTreeByLevel(headers, index, last, NULL, level + 1);
        }
    }
}

void VOutline::expandTree()
{
    if (topLevelItemCount() == 0) {
        return;
    }
    expandAll();
    setItemSelected(topLevelItem(0), true);
}


void VOutline::handleItemClicked(QTreeWidgetItem *item, int column)
{
    Q_ASSERT(item && column == 0);
    QJsonObject itemJson = item->data(0, Qt::UserRole).toJsonObject();
    QString anchor = itemJson["anchor"].toString();
    int lineNumber = itemJson["line_number"].toInt();
    qDebug() << "click anchor" << anchor << lineNumber;
    emit outlineItemActivated(VAnchor(outline.filePath, anchor, lineNumber));
}

void VOutline::updateCurHeader(const VAnchor &anchor)
{
    curHeader = anchor;
    if (outline.type == VHeaderType::Anchor) {
        selectAnchor(anchor.anchor);
    } else {
        // Select by lineNumber
    }
}

void VOutline::selectAnchor(const QString &anchor)
{
    QList<QTreeWidgetItem *> selected = selectedItems();
    foreach (QTreeWidgetItem *item, selected) {
        setItemSelected(item, false);
    }

    int nrTop = topLevelItemCount();
    for (int i = 0; i < nrTop; ++i) {
        if (selectAnchorOne(topLevelItem(i), anchor)) {
            return;
        }
    }
}

bool VOutline::selectAnchorOne(QTreeWidgetItem *item, const QString &anchor)
{
    if (!item) {
        return false;
    }
    QJsonObject itemJson = item->data(0, Qt::UserRole).toJsonObject();
    QString itemAnchor = itemJson["anchor"].toString();
    if (itemAnchor == anchor) {
        // Select this item
        setItemSelected(item, true);
        return true;
    }

    int nrChild = item->childCount();
    for (int i = 0; i < nrChild; ++i) {
        if (selectAnchorOne(item->child(i), anchor)) {
            return true;
        }
    }
    return false;
}
