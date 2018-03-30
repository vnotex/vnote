#ifndef VUNIVERSALENTRY_H
#define VUNIVERSALENTRY_H

#include <QWidget>
#include <QRect>
#include <QHash>
#include <QAtomicInt>

#include "iuniversalentry.h"

class VMetaWordLineEdit;
class QVBoxLayout;
class QHideEvent;
class QShowEvent;
class QPaintEvent;
class QKeyEvent;
class QTimer;
class VListWidget;

class VUniversalEntryContainer : public QWidget
{
    Q_OBJECT
public:
    VUniversalEntryContainer(QWidget *p_parent = nullptr);

    void clear();

    void setWidget(QWidget *p_widget);

    void adjustSizeByWidget();

protected:
    QSize sizeHint() const Q_DECL_OVERRIDE;

private:
    QWidget *m_widget;

    QVBoxLayout *m_layout;
};


class VUniversalEntry : public QWidget
{
    Q_OBJECT
public:
    explicit VUniversalEntry(QWidget *p_parent = nullptr);

    // Set the availabel width and height.
    void setAvailableRect(const QRect &p_rect);

    // Register an entry at @p_key.
    // Different entries may use the same @p_entry, in which case they can use
    // @p_id to distinguish.
    void registerEntry(QChar p_key, IUniversalEntry *p_entry, int p_id = 0);

signals:
    // Exit Universal Entry.
    void exited();

protected:
    void hideEvent(QHideEvent *p_event) Q_DECL_OVERRIDE;

    void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

    void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;

    void paintEvent(QPaintEvent *p_event) Q_DECL_OVERRIDE;

private:
    struct Entry
    {
        Entry()
            : m_entry(NULL), m_id(-1)
        {
        }

        Entry(IUniversalEntry *p_entry, int p_id)
            : m_entry(p_entry), m_id(p_id)
        {
        }

        IUniversalEntry *m_entry;
        int m_id;
    };

    void setupUI();

    void clear();

    void processCommand();

    void processCommand(const QString &p_cmd);

    void updateState(IUniversalEntry::State p_state);

    QString getCommandFromEdit() const;

    VMetaWordLineEdit *m_cmdEdit;

    VUniversalEntryContainer *m_container;

    // Rect availabel for the UE to use.
    QRect m_availableRect;

    int m_minimumWidth;

    QHash<QChar, Entry> m_entries;

    // Widget to list all entries.
    VListWidget *m_infoWidget;

    QTimer *m_cmdTimer;

    // Last used Entry.
    const Entry *m_lastEntry;

    // The CMD edit's original style sheet.
    QString m_cmdStyleSheet;

    QAtomicInt m_inQueue;

    bool m_pendingCommand;
};

#endif // VUNIVERSALENTRY_H
