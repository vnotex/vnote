#include "quickselector.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QListWidgetItem>
#include <QKeyEvent>
#include <QDebug>
#include <QRegularExpression>

#include <utils/widgetutils.h>
#include <utils/iconutils.h>
#include <core/thememgr.h>
#include <core/vnotex.h>

#include "lineedit.h"
#include "listwidget.h"
#include "widgetsfactory.h"

using namespace vnotex;

QuickSelectorItem::QuickSelectorItem(const QVariant &p_key,
                                     const QString &p_name,
                                     const QString &p_tip,
                                     const QString &p_shortcut)
    : m_key(p_key),
      m_name(p_name),
      m_tip(p_tip),
      m_shortcut(p_shortcut)
{
    Q_ASSERT(m_shortcut.size() < 3);
}

static bool selectorItemCmp(const QuickSelectorItem &p_a, const QuickSelectorItem &p_b)
{
    if (p_a.m_shortcut.isEmpty()) {
        if (p_b.m_shortcut.isEmpty()) {
            return p_a.m_name < p_b.m_name;
        }
        return false;
    } else {
        if (p_b.m_shortcut.isEmpty()) {
            return true;
        }

        return p_a.m_shortcut < p_b.m_shortcut;
    }
}

QuickSelector::QuickSelector(const QString &p_title,
                             const QVector<QuickSelectorItem> &p_items,
                             bool p_sortByShortcut,
                             QWidget *p_parent)
    : FloatingWidget(p_parent),
      m_items(p_items)
{
    if (p_sortByShortcut) {
        std::sort(m_items.begin(), m_items.end(), selectorItemCmp);
    }

    setupUI(p_title);

    updateItemList();
}

void QuickSelector::setupUI(const QString &p_title)
{
    auto mainLayout = new QVBoxLayout(this);

    if (!p_title.isEmpty()) {
        mainLayout->addWidget(new QLabel(p_title, this));
    }

    m_searchLineEdit = static_cast<LineEdit *>(WidgetsFactory::createLineEdit(this));
    m_searchLineEdit->setInputMethodEnabled(false);
    connect(m_searchLineEdit, &QLineEdit::textEdited,
            this, &QuickSelector::searchAndFilter);
    connect(m_searchLineEdit, &QLineEdit::returnPressed,
            this, [this]() {
                activateItem(m_itemList->currentItem());
            });
    mainLayout->addWidget(m_searchLineEdit);

    setFocusProxy(m_searchLineEdit);
    m_searchLineEdit->installEventFilter(this);

    m_itemList = new ListWidget(this);
    m_itemList->setWrapping(true);
    m_itemList->setFlow(QListView::LeftToRight);
    m_itemList->setIconSize(QSize(18, 18));
    connect(m_itemList, &QListWidget::itemActivated,
            this, &QuickSelector::activateItem);
    mainLayout->addWidget(m_itemList);

    m_itemList->installEventFilter(this);
}

void QuickSelector::updateItemList()
{
    m_itemList->clear();

    const auto &themeMgr = VNoteX::getInst().getThemeMgr();

    const auto fg = themeMgr.paletteColor(QStringLiteral("widgets#quickselector#item_icon#fg"));
    const auto border = themeMgr.paletteColor(QStringLiteral("widgets#quickselector#item_icon#border"));

    for (int i = 0; i < m_items.size(); ++i) {
        const auto &item = m_items[i];

        const auto icon = IconUtils::drawTextIcon(item.m_shortcut, fg, border);
        auto listItem = new QListWidgetItem(icon, item.m_name, m_itemList);

        listItem->setToolTip(item.m_tip);
        listItem->setData(Qt::UserRole, i);
    }

    Q_ASSERT(!m_items.isEmpty());
    m_itemList->setCurrentRow(0);
}

void QuickSelector::activateItem(const QListWidgetItem *p_item)
{
    if (p_item) {
        m_selectedKey = getSelectorItem(p_item).m_key;
    }
    finish();
}

void QuickSelector::activate(const QuickSelectorItem *p_item)
{
    m_selectedKey = p_item->m_key;
    finish();
}

QuickSelectorItem &QuickSelector::getSelectorItem(const QListWidgetItem *p_item)
{
    Q_ASSERT(p_item);
    return m_items[p_item->data(Qt::UserRole).toInt()];
}

QVariant QuickSelector::result() const
{
    return m_selectedKey;
}

bool QuickSelector::eventFilter(QObject *p_obj, QEvent *p_event)
{
    if ((p_obj == m_searchLineEdit || p_obj == m_itemList)
        && p_event->type() == QEvent::KeyPress) {
        auto keyEve = static_cast<QKeyEvent *>(p_event);
        const auto key = keyEve->key();
        if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
            // Change focus.
            if (p_obj == m_searchLineEdit) {
                m_itemList->setFocus();
            } else {
                m_searchLineEdit->setFocus();
            }
            return true;
        }
    }
    return FloatingWidget::eventFilter(p_obj, p_event);
}

void QuickSelector::searchAndFilter(const QString &p_text)
{
    auto text = p_text.trimmed();
    if (text.isEmpty()) {
        // Show all items.
        filterItems([](const QuickSelectorItem &) {
                return true;
            });
        return;
    } else if (text.size() < 3) {
        // Check shortcut first.
        const QuickSelectorItem *hitItem = nullptr;
        int ret = filterItems([&text, &hitItem](const QuickSelectorItem &p_item) {
                if (p_item.m_shortcut == text) {
                    hitItem = &p_item;
                    return true;
                } else if (p_item.m_shortcut.startsWith(text)) {
                    return true;
                }
                return false;
            });

        if (hitItem) {
            activate(hitItem);
            return;
        }

        if (ret > 0) {
            return;
        }
    }

    // Check name.
    auto parts = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    Q_ASSERT(!parts.isEmpty());
    QRegularExpression regExp;
    regExp.setPatternOptions(regExp.patternOptions() | QRegularExpression::CaseInsensitiveOption);
    if (parts.size() == 1) {
        regExp.setPattern(QRegularExpression::escape(parts[0]));
    } else {
        QString pattern = QRegularExpression::escape(parts[0]);
        for (int i = 1; i < parts.size(); ++i) {
            pattern += ".*" + QRegularExpression::escape(parts[i]);
        }
        regExp.setPattern(pattern);
    }
    filterItems([&regExp](const QuickSelectorItem &p_item) {
            if (p_item.m_name.indexOf(regExp) != -1) {
                return true;
            }
            return false;
        });
}

int QuickSelector::filterItems(const std::function<bool(const QuickSelectorItem &)> &p_judge)
{
    const int cnt = m_itemList->count();
    int matchedCnt = 0;
    int firstHit = -1;
    for (int i = 0; i < cnt; ++i) {
        auto item = m_itemList->item(i);
        bool hit = p_judge(getSelectorItem(item));
        if (hit) {
            if (matchedCnt == 0) {
                firstHit = i;
            }
            ++matchedCnt;
        }
        item->setHidden(!hit);
    }
    m_itemList->setCurrentRow(firstHit);
    return matchedCnt;
}
