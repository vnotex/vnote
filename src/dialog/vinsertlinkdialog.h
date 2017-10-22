#ifndef VINSERTLINKDIALOG_H
#define VINSERTLINKDIALOG_H

#include <QDialog>
#include <QString>

class VLineEdit;
class QLineEdit;
class QDialogButtonBox;

class VInsertLinkDialog : public QDialog
{
    Q_OBJECT
public:
    VInsertLinkDialog(const QString &p_title,
                      const QString &p_text,
                      const QString &p_info,
                      const QString &p_linkText,
                      const QString &p_linkUrl,
                      QWidget *p_parent = nullptr);

    QString getLinkText() const;

    QString getLinkUrl() const;

private slots:
    void handleInputChanged();

private:
    void setupUI(const QString &p_title,
                 const QString &p_text,
                 const QString &p_info,
                 const QString &p_linkText,
                 const QString &p_linkUrl);

    // Infer link text/url from clipboard only when text and url are both empty.
    void fetchLinkFromClipboard();

    VLineEdit *m_linkTextEdit;

    QLineEdit *m_linkUrlEdit;

    QDialogButtonBox *m_btnBox;
};

#endif // VINSERTLINKDIALOG_H
