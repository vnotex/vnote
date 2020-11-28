#ifndef FOLDERFILESFILTERWIDGET_H
#define FOLDERFILESFILTERWIDGET_H

#include <QWidget>
#include <QStringList>

class QTimer;
class QLineEdit;
class QListWidget;
class QListWidgetItem;

namespace vnotex
{
    class SelectionItemWidget;

    // Filter files within a folder by suffix.
    class FolderFilesFilterWidget : public QWidget
    {
        Q_OBJECT
    public:
        explicit FolderFilesFilterWidget(QWidget *p_parent = nullptr);

        QLineEdit *getFolderPathEdit() const;

        QString getFolderPath() const;

        QStringList getSuffixes() const;

        // Whether complete scanning files.
        bool isReady() const;

    signals:
        // Folder path or selected suffixes changed.
        void filesChanged();

    private slots:
        void scanSuffixes();

    private:
        void setupUI();

        SelectionItemWidget *getItemWidget(QListWidgetItem *p_item) const;

        // Managed by QObject.
        QTimer *m_scanTimer = nullptr;

        QLineEdit *m_folderPathEdit = nullptr;

        QListWidget *m_suffixList = nullptr;

        bool m_ready = false;
    };
}

#endif // FOLDERFILESFILTERWIDGET_H
