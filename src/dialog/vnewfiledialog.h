#ifndef VNEWFILEDIALOG_H
#define VNEWFILEDIALOG_H

#include <QDialog>

#include "vconstants.h"

class QLabel;
class VMetaWordLineEdit;
class QDialogButtonBox;
class QCheckBox;
class VDirectory;
class QComboBox;
class QTextEdit;

class VNewFileDialog : public QDialog
{
    Q_OBJECT
public:
    VNewFileDialog(const QString &p_title,
                   const QString &p_info,
                   const QString &p_defaultName,
                   VDirectory *p_directory,
                   QWidget *p_parent = 0);

    QString getNameInput() const;

    bool getInsertTitleInput() const;

    // Whether user choose a note template.
    bool isTemplateUsed() const;

    // Get the template content (after magic words evaluated) user chose.
    QString getTemplate() const;

private slots:
    void handleInputChanged();

    void handleCurrentTemplateChanged(int p_idx);

private:
    void setupUI(const QString &p_title,
                 const QString &p_info,
                 const QString &p_defaultName);

    // Update the templates according to @p_type.
    void updateTemplates(DocType p_type);

    void enableInsertTitleCB(bool p_hasTemplate);

    void tryToSelectLastTemplate();

    VMetaWordLineEdit *m_nameEdit;

    QComboBox *m_templateCB;

    // Used for template preview.
    QTextEdit *m_templateEdit;

    QCheckBox *m_insertTitleCB;

    QPushButton *okBtn;
    QDialogButtonBox *m_btnBox;

    QLabel *m_warnLabel;

    // Template content.
    QString m_template;

    // Doc type of current template.
    DocType m_currentTemplateType;

    // Last chosen template file.
    static QString s_lastTemplateFile;

    VDirectory *m_directory;
};

#endif // VNEWFILEDIALOG_H
