#ifndef VLISTFOLDERUE_H
#define VLISTFOLDERUE_H

#include "iuniversalentry.h"
#include <QWidget>
#include <QIcon>

#include "vsearchconfig.h"

class VListWidgetDoubleRows;
class QListWidgetItem;
class QLabel;

class VListFolderPanel : public QWidget
{
    Q_OBJECT
public:
    explicit VListFolderPanel(QWidget *p_contentWidget,
                              QWidget *p_parent = nullptr);

    void setTitleLabel(const QString &p_title);

    void clearTitle();

private:
    QLabel *m_titleLabel;
};


// Universal Entry to list contents of folder.
class VListFolderUE : public IUniversalEntry
{
    Q_OBJECT
public:
    explicit VListFolderUE(QObject *p_parent = nullptr);

    QString description(int p_id) const Q_DECL_OVERRIDE;

    QWidget *widget(int p_id) Q_DECL_OVERRIDE;

    void processCommand(int p_id, const QString &p_cmd) Q_DECL_OVERRIDE;

    void clear(int p_id) Q_DECL_OVERRIDE;

    void selectNextItem(int p_id, bool p_forward) Q_DECL_OVERRIDE;

    void selectParentItem(int p_id) Q_DECL_OVERRIDE;

    void activate(int p_id) Q_DECL_OVERRIDE;

    void sort(int p_id) Q_DECL_OVERRIDE;

    void entryShown(int p_id, const QString &p_cmd) Q_DECL_OVERRIDE;

    QString currentItemFolder(int p_id) Q_DECL_OVERRIDE;

    void setFolderPath(const QString &p_path);

protected:
    void init() Q_DECL_OVERRIDE;

private slots:
    void activateItem(QListWidgetItem *p_item);

private:
    void addResultItem(const QSharedPointer<VSearchResultItem> &p_item);

    const QSharedPointer<VSearchResultItem> &itemResultData(const QListWidgetItem *p_item) const;

    bool listFolder(const QString &p_path, const QString &p_cmd);

    QVector<QSharedPointer<VSearchResultItem> > m_data;

    QIcon m_noteIcon;
    QIcon m_folderIcon;

    // Folder path to list.
    // If empty, use current folder.
    QString m_folderPath;

    // Current folder path.
    QString m_currentFolderPath;

    VListWidgetDoubleRows *m_listWidget;

    VListFolderPanel *m_panel;
};

#endif // VLISTFOLDERUE_H
