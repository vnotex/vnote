#include "selectdialog.h"

#include <QtWidgets>

#include <widgets/listwidget.h>
#include <utils/widgetutils.h>
#include <utils/iconutils.h>
#include <utils/utils.h>
#include <core/thememgr.h>
#include <core/vnotex.h>

using namespace vnotex;

const QChar SelectDialog::c_cancelShortcut = QLatin1Char('z');

SelectDialog::SelectDialog(const QString &p_title, QWidget *p_parent)
    : SelectDialog(p_title, QString(), p_parent)
{
}

SelectDialog::SelectDialog(const QString &p_title,
                           const QString &p_text,
                           QWidget *p_parent)
    : QDialog(p_parent)
{
    const auto &themeMgr = VNoteX::getInst().getThemeMgr();
    m_shortcutIconForeground = themeMgr.paletteColor(QStringLiteral("widgets#quickselector#item_icon#fg"));
    m_shortcutIconBorder = themeMgr.paletteColor(QStringLiteral("widgets#quickselector#item_icon#border"));

    setupUI(p_title, p_text);
}

void SelectDialog::setupUI(const QString &p_title, const QString &p_text)
{
    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    if (!p_text.isNull()) {
        m_label = new QLabel(p_text, this);
        mainLayout->addWidget(m_label);
    }

    m_list = new ListWidget(this);
    m_list->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setAttribute(Qt::WA_MacShowFocusRect, false);
    connect(m_list, &ListWidget::itemActivated,
            this, &SelectDialog::selectionChosen);

    m_list->installEventFilter(this);

    // Add cancel item.
    {
        const auto icon = IconUtils::drawTextIcon(c_cancelShortcut, m_shortcutIconForeground, m_shortcutIconBorder);
        QListWidgetItem *cancelItem = new QListWidgetItem(icon, tr("Cancel"));
        cancelItem->setData(Qt::UserRole, CANCEL_ID);

        m_shortcuts.insert(c_cancelShortcut, cancelItem);

        m_list->addItem(cancelItem);
        m_list->setCurrentRow(0);
    }

    mainLayout->addWidget(m_list);

    setWindowTitle(p_title);
}

void SelectDialog::addSelection(const QString &p_selectStr, int p_selectID)
{
    Q_ASSERT(p_selectID >= 0);

    QChar shortcut;
    if (m_nextShortcut < c_cancelShortcut) {
        shortcut = m_nextShortcut;
        m_nextShortcut = QChar(m_nextShortcut.toLatin1() + 1);
    }
    const auto icon = IconUtils::drawTextIcon(shortcut, m_shortcutIconForeground, m_shortcutIconBorder);
    QListWidgetItem *item = new QListWidgetItem(icon, p_selectStr);
    item->setData(Qt::UserRole, p_selectID);

    if (!shortcut.isNull()) {
        m_shortcuts.insert(shortcut, item);
    }

    m_list->insertItem(m_list->count() - 1, item);

    m_list->setCurrentRow(0);
}

void SelectDialog::selectionChosen(QListWidgetItem *p_item)
{
    m_choice = p_item->data(Qt::UserRole).toInt();
    if (m_choice == CANCEL_ID) {
        reject();
    } else {
        accept();
    }
}

int SelectDialog::getSelection() const
{
    return m_choice;
}

void SelectDialog::updateSize()
{
    Q_ASSERT(m_list->count() > 0);

    int height = 0;
    for (int i = 0; i < m_list->count(); ++i) {
        height += m_list->sizeHintForRow(i);
    }

    height += 2 * m_list->count();
    int wid = width();
    m_list->resize(wid, height);
    resize(wid, height);
}

void SelectDialog::showEvent(QShowEvent *p_event)
{
    QDialog::showEvent(p_event);

    updateSize();
}

void SelectDialog::keyPressEvent(QKeyEvent *p_event)
{
    if (WidgetUtils::processKeyEventLikeVi(m_list, p_event, this)) {
        return;
    }

    QDialog::keyPressEvent(p_event);
}

bool SelectDialog::eventFilter(QObject *p_watched, QEvent *p_event)
{
    if (p_watched == m_list && p_event->type() == QEvent::KeyPress) {
        auto keyEve = static_cast<QKeyEvent *>(p_event);
        // Handle shortcuts.
        if (keyEve->modifiers() == Qt::NoModifier) {
            auto keych = Utils::keyToChar(keyEve->key(), true);
            if (keych.isLetter()) {
                auto it = m_shortcuts.find(keych);
                if (it != m_shortcuts.end()) {
                    selectionChosen(it.value());
                }

                return true;
            }
        }
    }

    return QDialog::eventFilter(p_watched, p_event);
}
