#ifndef FILEPROPERTIESDIALOG_H
#define FILEPROPERTIESDIALOG_H

#include "scrolldialog.h"

class QLineEdit;

namespace vnotex
{
    class FilePropertiesDialog : public ScrollDialog
    {
        Q_OBJECT
    public:
        FilePropertiesDialog(const QString &p_path, QWidget *p_parent = nullptr);

        QString getFileName() const;

    private:
        void setupUI();

        void setupNameLineEdit(QWidget *p_parent);

        QString m_path;

        QLineEdit *m_nameLineEdit = nullptr;
    };
}

#endif // FILEPROPERTIESDIALOG_H
