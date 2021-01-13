#include "widgetutils.h"

#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <QStyle>
#include <QAbstractScrollArea>
#include <QScrollBar>
#include <QDesktopWidget>
#include <QApplication>
#include <QDesktopServices>
#include <QKeyEvent>
#include <QActionGroup>
#include <QAction>
#include <QKeySequence>
#include <QScrollArea>
#include <QTimer>
#include <QShortcut>
#include <QListView>
#include <QModelIndex>
#include <QFontDatabase>
#include <QMenu>
#include <QDebug>
#include <QLineEdit>

using namespace vnotex;

void WidgetUtils::setPropertyDynamically(QWidget *p_widget,
                                         const char *p_prop,
                                         const QVariant &p_val)
{
    p_widget->setProperty(p_prop, p_val);
    updateStyle(p_widget);
}

void WidgetUtils::updateStyle(QWidget *p_widget)
{
    p_widget->style()->unpolish(p_widget);
    p_widget->style()->polish(p_widget);
    p_widget->update();
}

qreal WidgetUtils::calculateScaleFactor(bool p_update)
{
    static qreal factor = -1;

    if (factor < 0 || p_update) {
        factor =  QGuiApplication::primaryScreen()->devicePixelRatio();
    }

    return factor;
}

bool WidgetUtils::isScrollBarVisible(QAbstractScrollArea *p_widget, bool p_horizontal)
{
    auto scrollBar = p_horizontal ? p_widget->horizontalScrollBar() : p_widget->verticalScrollBar();
    if (scrollBar && scrollBar->isVisible() && scrollBar->minimum() != scrollBar->maximum()) {
        return true;
    }

    return false;
}

QSize WidgetUtils::availableScreenSize(QWidget *p_widget)
{
    auto geo = QApplication::desktop()->availableGeometry(p_widget);
    return geo.size();
}

void WidgetUtils::openUrlByDesktop(const QUrl &p_url)
{
    QDesktopServices::openUrl(p_url);
}

bool WidgetUtils::processKeyEventLikeVi(QWidget *p_widget,
                                        QKeyEvent *p_event,
                                        QWidget *p_escTargetWidget)
{
    Q_ASSERT(p_widget);

    bool eventHandled = false;
    int key = p_event->key();
    int modifiers = p_event->modifiers();
    if (!p_escTargetWidget) {
        p_escTargetWidget = p_widget;
    }

    switch (key) {
    case Qt::Key_BracketLeft:
    {
        if (isViControlModifier(modifiers)) {
            auto escEvent = new QKeyEvent(QEvent::KeyPress,
                                          Qt::Key_Escape,
                                          Qt::NoModifier);
            QCoreApplication::postEvent(p_escTargetWidget, escEvent);
            eventHandled = true;
        }

        break;
    }

    case Qt::Key_J:
    {
        if (isViControlModifier(modifiers)) {
            // The event must be allocated on the heap since the post event queue will take ownership
            // of the event and delete it once it has been posted.
            auto downEvent = new QKeyEvent(QEvent::KeyPress,
                                           Qt::Key_Down,
                                           Qt::NoModifier);
            QCoreApplication::postEvent(p_widget, downEvent);
            eventHandled = true;
        }

        break;
    }

    case Qt::Key_K:
    {
        if (isViControlModifier(modifiers)) {
            auto upEvent = new QKeyEvent(QEvent::KeyPress,
                                         Qt::Key_Up,
                                         Qt::NoModifier);
            QCoreApplication::postEvent(p_widget, upEvent);
            eventHandled = true;
        }

        break;
    }

    default:
        break;
    }

    if (eventHandled) {
        p_event->accept();
    }

    return eventHandled;
}

bool WidgetUtils::isViControlModifier(int p_modifiers)
{
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    return p_modifiers == Qt::MetaModifier;
#else
    return p_modifiers == Qt::ControlModifier;
#endif
}

void WidgetUtils::clearActionGroup(QActionGroup *p_actGroup)
{
    auto actions = p_actGroup->actions();
    for (auto action : actions) {
        p_actGroup->removeAction(action);
    }
}

void WidgetUtils::addActionShortcut(QAction *p_action,
                                    const QString &p_shortcut,
                                    Qt::ShortcutContext p_context)
{
    QKeySequence kseq(p_shortcut);
    if (kseq.isEmpty()) {
        return;
    }

    p_action->setShortcut(kseq);
    p_action->setShortcutContext(p_context);
    p_action->setText(QString("%1\t%2").arg(p_action->text(), kseq.toString(QKeySequence::NativeText)));
}

void WidgetUtils::addActionShortcutText(QAction *p_action,
                                        const QString &p_shortcut)
{
    QKeySequence kseq(p_shortcut);
    if (kseq.isEmpty()) {
        return;
    }

    p_action->setText(QString("%1\t%2").arg(p_action->text(), kseq.toString(QKeySequence::NativeText)));
}

void WidgetUtils::updateSize(QWidget *p_widget)
{
    p_widget->adjustSize();
    p_widget->updateGeometry();
}

void WidgetUtils::resizeToHideScrollBarLater(QScrollArea *p_scroll, bool p_vertical, bool p_horizontal)
{
    QTimer::singleShot(200, p_scroll, [p_scroll, p_vertical, p_horizontal]() {
        WidgetUtils::resizeToHideScrollBar(p_scroll, p_vertical, p_horizontal);
    });
}

void WidgetUtils::resizeToHideScrollBar(QScrollArea *p_scroll, bool p_vertical, bool p_horizontal)
{
    bool changed = false;
    auto parentWidget = p_scroll->parentWidget();

    if (p_horizontal && WidgetUtils::isScrollBarVisible(p_scroll, true)) {
        auto scrollBar = p_scroll->horizontalScrollBar();
        auto delta = scrollBar->maximum() - scrollBar->minimum();
        auto availableSize = WidgetUtils::availableScreenSize(p_scroll);

        if (parentWidget) {
            int newWidth = parentWidget->width() + delta;
            if (newWidth <= availableSize.width()) {
                changed = true;
                p_scroll->resize(p_scroll->width() + delta, p_scroll->height());
                auto geo = parentWidget->geometry();
                parentWidget->setGeometry(geo.x() - delta / 2,
                                          geo.y(),
                                          newWidth,
                                          geo.height());
            }
        } else {
            int newWidth = p_scroll->width() + delta;
            if (newWidth <= availableSize.width()) {
                changed = true;
                p_scroll->resize(newWidth, p_scroll->height());
            }
        }
    }

    if (p_vertical && WidgetUtils::isScrollBarVisible(p_scroll, false)) {
        auto scrollBar = p_scroll->verticalScrollBar();
        auto delta = scrollBar->maximum() - scrollBar->minimum();
        auto availableSize = WidgetUtils::availableScreenSize(p_scroll);

        if (parentWidget) {
            int newHeight = parentWidget->height() + delta;
            if (newHeight <= availableSize.height()) {
                changed = true;
                p_scroll->resize(p_scroll->width(), p_scroll->height() + delta);
                auto geo = parentWidget->geometry();
                parentWidget->setGeometry(geo.x(),
                                          geo.y() - delta / 2,
                                          geo.width(),
                                          newHeight);
            }
        } else {
            int newHeight = p_scroll->height() + delta;
            if (newHeight <= availableSize.height()) {
                changed = true;
                p_scroll->resize(p_scroll->width(), newHeight);
            }
        }
    }

    if (changed) {
        p_scroll->updateGeometry();
    }
}

QShortcut *WidgetUtils::createShortcut(const QString &p_shortcut,
                                       QWidget *p_widget,
                                       Qt::ShortcutContext p_context)
{
    QKeySequence kseq(p_shortcut);
    if (kseq.isEmpty()) {
        return nullptr;
    }

    auto shortcut = new QShortcut(kseq, p_widget, nullptr, nullptr, p_context);
    return shortcut;
}

bool WidgetUtils::isMetaKey(int p_key)
{
    return p_key == Qt::Key_Control
           || p_key == Qt::Key_Shift
           || p_key == Qt::Key_Meta
#if defined(Q_OS_LINUX)
           // For mapping Caps as Ctrl in KDE.
           || p_key == Qt::Key_CapsLock
#endif
           || p_key == Qt::Key_Alt;
}

QVector<QModelIndex> WidgetUtils::getVisibleIndexes(const QListView *p_view)
{
    QVector<QModelIndex> indexes;

    auto firstItem = p_view->indexAt(QPoint(0, 0));
    if (!firstItem.isValid()) {
        return indexes;
    }

    auto lastItem = p_view->indexAt(p_view->viewport()->rect().bottomLeft());

    int firstRow = firstItem.row();
    int lastRow = lastItem.isValid() ? lastItem.row() : (p_view->model()->rowCount() - 1);
    for (int i = firstRow; i <= lastRow; ++i) {
        if (p_view->isRowHidden(i)) {
            continue;
        }
        auto item = firstItem.siblingAtRow(i);
        if (item.isValid()) {
            indexes.append(item);
        }
    }

    return indexes;
}

QString WidgetUtils::getMonospaceFont()
{
    static QString font;
    if (font.isNull()) {
        QStringList candidates;
        candidates << QStringLiteral("YaHei Consolas Hybrid")
                   << QStringLiteral("Consolas")
                   << QStringLiteral("Monaco")
                   << QStringLiteral("Andale Mono")
                   << QStringLiteral("Monospace")
                   << QStringLiteral("Courier New");
        auto availFamilies = QFontDatabase().families();
        for (const auto &candidate : candidates) {
            QString family = candidate.trimmed().toLower();
            for (auto availFamily : availFamilies) {
                availFamily.remove(QRegExp("\\[.*\\]"));
                if (family == availFamily.trimmed().toLower()) {
                    font = availFamily;
                    return font;
                }
            }
        }

        // Fallback to current font.
        font = QFont().family();
    }

    return font;
}

QAction *WidgetUtils::findActionByObjectName(const QList<QAction *> &p_actions, const QString &p_objName)
{
    for (auto act : p_actions) {
        if (act->objectName() == p_objName) {
            return act;
        }
    }

    return nullptr;
}

// Insert @p_action into @p_menu after action @p_after.
void WidgetUtils::insertActionAfter(QMenu *p_menu, QAction *p_after, QAction *p_action)
{
    p_menu->insertAction(p_after, p_action);
    if (p_after) {
        p_menu->removeAction(p_after);
        p_menu->insertAction(p_action, p_after);
    }
}

void WidgetUtils::selectBaseName(QLineEdit *p_lineEdit)
{
    auto text = p_lineEdit->text();
    int dotIndex = text.lastIndexOf(QLatin1Char('.'));
    p_lineEdit->setSelection(0, (dotIndex == -1) ? text.size() : dotIndex);
}
