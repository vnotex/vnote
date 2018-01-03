#ifndef VCOPYTEXTASHTMLDIALOG_H
#define VCOPYTEXTASHTMLDIALOG_H

#include <QDialog>
#include <QUrl>


class QPlainTextEdit;
class QWebEngineView;
class QDialogButtonBox;
class VWaitingWidget;
class QLabel;

class VCopyTextAsHtmlDialog : public QDialog
{
    Q_OBJECT
public:
    VCopyTextAsHtmlDialog(const QString &p_text, QWidget *p_parent = nullptr);

    void setConvertedHtml(const QUrl &p_baseUrl, const QString &p_html);

    const QString &getText() const;

private:
    void setupUI();

    void setHtmlVisible(bool p_visible);

    QPlainTextEdit *m_textEdit;

    QLabel *m_htmlLabel;

    QWebEngineView *m_htmlViewer;

    QLabel *m_infoLabel;

    QDialogButtonBox *m_btnBox;

    QString m_text;
};

inline const QString &VCopyTextAsHtmlDialog::getText() const
{
    return m_text;
}
#endif // VCOPYTEXTASHTMLDIALOG_H
