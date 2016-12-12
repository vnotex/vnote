#include <QDebug>
#include <QVector>
#include <QString>
#include <QJsonObject>
#include <QKeyEvent>
#include "voutline.h"
#include "vtoc.h"

VOutline::VOutline(QWidget *parent)
    : QTreeWidget(parent)
{
    setColumnCount(1);
    setHeaderHidden(true);
    setSelectionMode(QAbstractItemView::SingleSelection);

    connect(this, &VOutline::currentItemChanged,
            this, &VOutline::handleCurItemChanged);
}

void VOutline::updateOutline(const VToc &toc)
{
    // Clear current header
    curHeader = VAnchor();
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
            itemJson["outline_index"] = index;
            item->setData(0, Qt::UserRole, itemJson);
            item->setText(0, header.name);
            item->setToolTip(0, header.name);

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
}

void VOutline::handleCurItemChanged(QTreeWidgetItem *p_curItem, QTreeWidgetItem *p_preItem)
{
    if (!p_curItem) {
        return;
    }
    QJsonObject itemJson = p_curItem->data(0, Qt::UserRole).toJsonObject();
    QString anchor = itemJson["anchor"].toString();
    int lineNumber = itemJson["line_number"].toInt();
    int outlineIndex = itemJson["outline_index"].toInt();
    VAnchor tmp;
    tmp.filePath = outline.filePath;
    tmp.anchor = anchor;
    tmp.lineNumber = lineNumber;
    tmp.m_outlineIndex = outlineIndex;
    if (tmp == curHeader) {
        return;
    }
    curHeader = tmp;
    qDebug() << "current header changed" << tmp.anchor << tmp.lineNumber;
    emit outlineItemActivated(curHeader);
}

void VOutline::updateCurHeader(const VAnchor &anchor)
{
    qDebug() << "update current header" << anchor.anchor << anchor.lineNumber;
    if (anchor == curHeader) {
        return;
    }
    curHeader = anchor;
    if (outline.type == VHeaderType::Anchor) {
        selectAnchor(anchor.anchor);
    } else {
        // Select by lineNumber
        selectLineNumber(anchor.lineNumber);
    }
}

void VOutline::selectAnchor(const QString &anchor)
{
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
        setCurrentItem(item);
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

void VOutline::selectLineNumber(int lineNumber)
{
    int nrTop = topLevelItemCount();
    for (int i = 0; i < nrTop; ++i) {
        if (selectLineNumberOne(topLevelItem(i), lineNumber)) {
            return;
        }
    }
}

bool VOutline::selectLineNumberOne(QTreeWidgetItem *item, int lineNumber)
{
    if (!item) {
        return false;
    }
    QJsonObject itemJson = item->data(0, Qt::UserRole).toJsonObject();
    int itemLineNum = itemJson["line_number"].toInt();
    if (itemLineNum == lineNumber) {
        // Select this item
        setCurrentItem(item);
        return true;
    }

    int nrChild = item->childCount();
    for (int i = 0; i < nrChild; ++i) {
        if (selectLineNumberOne(item->child(i), lineNumber)) {
            return true;
        }
    }
    return false;
}

void VOutline::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return) {
        QTreeWidgetItem *item = currentItem();
        if (item) {
            item->setExpanded(!item->isExpanded());
        }
    }
    QTreeWidget::keyPressEvent(event);
}
