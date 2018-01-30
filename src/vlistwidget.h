#ifndef VLISTWIDGET_H
#define VLISTWIDGET_H

#include <QListWidget>
#include <QPainter>
#include <QStyleOptionViewItem>
#include <QModelIndex>
#include <QItemDelegate>
#include <QDebug>
#include <QLabel>

class VLineEdit;

class VItemDelegate : public QItemDelegate
{
public:
    explicit VItemDelegate(QObject *parent = Q_NULLPTR)
        : QItemDelegate(parent), m_searchKey()
    {
    }

    void paint(QPainter *painter,
               const QStyleOptionViewItem &option,
               const QModelIndex &index) const Q_DECL_OVERRIDE
    {
        painter->save();
        QPainter::CompositionMode oldCompMode = painter->compositionMode();
        // set background color
        painter->setPen(QPen(Qt::NoPen));
        if (option.state & QStyle::State_Selected) {
            // TODO: make it configuable
            painter->setBrush(QBrush(QColor("#d3d3d3")));
        } else {
            // use default brush
        }

        painter->drawRect(option.rect);

        Qt::GlobalColor hitPenColor = Qt::blue;
        Qt::GlobalColor normalPenColor = Qt::black;

        // set text color
        QVariant value = index.data(Qt::DisplayRole);
        QRectF rect(option.rect), boundRect;
        if (value.isValid()) {
            QString text = value.toString();
            int idx;
            bool isHit = !m_searchKey.isEmpty()
                         && (idx = text.indexOf(m_searchKey, 0, Qt::CaseInsensitive)) != -1;
            if (isHit) {
                qDebug() << QString("highlight: %1 (with: %2)").arg(text).arg(m_searchKey);
                // split the text by the search key
                QString left = text.left(idx), right = text.mid(idx + m_searchKey.length());
                drawText(painter, normalPenColor, rect, Qt::AlignLeft, left, boundRect);
                drawText(painter, hitPenColor, rect, Qt::AlignLeft, m_searchKey, boundRect);

                // highlight matched keyword
                painter->setBrush(QBrush(QColor("#ffde7b")));
                painter->setCompositionMode(QPainter::CompositionMode_Multiply);
                painter->setPen(Qt::NoPen);
                painter->drawRect(boundRect);
                painter->setCompositionMode(oldCompMode);

                drawText(painter, normalPenColor, rect, Qt::AlignLeft, right, boundRect);
            } else {
                drawText(painter, normalPenColor, rect, Qt::AlignLeft, text, boundRect);
            }
        }

        painter->restore();
    }

    void drawText(QPainter *painter,
                  Qt::GlobalColor penColor,
                  QRectF& rect,
                  int flags,
                  QString text,
                  QRectF& boundRect) const
    {
        if (!text.isEmpty()) {
            painter->setPen(QPen(penColor));
            painter->drawText(rect, flags, text, &boundRect);
            rect.adjust(boundRect.width(), 0, boundRect.width(), 0);
        }
    }

    void setSearchKey(const QString& key)
    {
        m_searchKey = key;
    }

private:
    QString m_searchKey;
};

class VListWidget : public QListWidget
{
public:
    explicit VListWidget(QWidget *parent = Q_NULLPTR);

    void selectItem(QListWidgetItem *item);

    void exitSearchMode(bool restoreSelection=true);

    void enterSearchMode();

    void refresh();

public slots:
    void clear();

private slots:
    void handleSearchKeyChanged(const QString& updatedText);

protected:
    void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;

private:
    QLabel *m_label;
    VLineEdit* m_searchKey;
    bool m_isInSearch;

    VItemDelegate* m_delegateObj;

    // Items that are matched by the search key.
    QList<QListWidgetItem*> m_hitItems;

    // How many items are matched, if no search key or key is empty string,
    // all items are matched.
    int m_hitCount;

    // Current selected item index.
    int m_curItemIdx;

    // Current selected item.
    QListWidgetItem* m_curItem;
};

#endif // VLISTWIDGET_H
