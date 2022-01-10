#ifndef STYLEDITEMDELEGATE_H
#define STYLEDITEMDELEGATE_H

#include <QStyledItemDelegate>
#include <QSharedPointer>
#include <QBrush>
#include <QList>

#include <core/global.h>

class QTextDocument;

namespace vnotex
{
    class ListWidget;
    class TreeWidget;
    class SimpleSegmentHighlighter;

    class StyledItemDelegateInterface
    {
    public:
        virtual ~StyledItemDelegateInterface() = default;
    };


    class StyledItemDelegateListWidget : public StyledItemDelegateInterface
    {
    public:
        explicit StyledItemDelegateListWidget(const ListWidget *p_listWidget);
    };


    class StyledItemDelegateTreeWidget : public StyledItemDelegateInterface
    {
    public:
        explicit StyledItemDelegateTreeWidget(const TreeWidget *p_treeWidget);
    };


    // Template is not supported with QObject.
    class StyledItemDelegate : public QStyledItemDelegate
    {
        Q_OBJECT
    public:
        enum DelegateFlag
        {
            None = 0,
            Highlights = 0x1
        };
        Q_DECLARE_FLAGS(DelegateFlags, DelegateFlag);

        StyledItemDelegate(const QSharedPointer<StyledItemDelegateInterface> &p_interface,
                           DelegateFlags p_flags = DelegateFlag::None,
                           QObject *p_parent = nullptr);

        void paint(QPainter *p_painter,
                   const QStyleOptionViewItem &p_option,
                   const QModelIndex &p_index) const Q_DECL_OVERRIDE;

        QSize sizeHint(const QStyleOptionViewItem &p_option, const QModelIndex &p_index) const Q_DECL_OVERRIDE;

        static QBrush s_highlightForeground;

        static QBrush s_highlightBackground;

    private:
        void initialize();

        void paintWithHighlights(QPainter *p_painter,
                                 const QStyleOptionViewItem &p_option,
                                 const QModelIndex &p_index,
                                 const QList<Segment> &p_segments) const;

        QSharedPointer<StyledItemDelegateInterface> m_interface;

        DelegateFlags m_flags = DelegateFlag::None;

        QTextDocument *m_document = nullptr;

        SimpleSegmentHighlighter *m_highlighter = nullptr;
    };
}

Q_DECLARE_OPERATORS_FOR_FLAGS(vnotex::StyledItemDelegate::DelegateFlags)

#endif // STYLEDITEMDELEGATE_H
