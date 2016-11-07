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
    expandAll();
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
