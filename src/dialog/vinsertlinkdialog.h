#ifndef VINSERTLINKDIALOG_H
#define VINSERTLINKDIALOG_H

#include <QDialog>
#include <QString>

class VMetaWordLineEdit;
class VLineEdit;
class QDialogButtonBox;
class QShowEvent;

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

protected:
    void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

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

    VMetaWordLineEdit *m_linkTextEdit;

    VLineEdit *m_linkUrlEdit;

    QDialogButtonBox *m_btnBox;
};

#endif // VINSERTLINKDIALOG_H
