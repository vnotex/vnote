#ifndef VEDITSNIPPETDIALOG_H
#define VEDITSNIPPETDIALOG_H

#include <QDialog>
#include <QVector>

#include "vsnippet.h"

class VMetaWordLineEdit;
class VLineEdit;
class QLabel;
class QDialogButtonBox;
class QComboBox;
class QTextEdit;
class QCheckBox;


class VEditSnippetDialog : public QDialog
{
    Q_OBJECT
public:
    VEditSnippetDialog(const QString &p_title,
                       const QString &p_info,
                       const QVector<VSnippet> &p_snippets,
                       const VSnippet &p_snippet,
                       QWidget *p_parent = nullptr);

    QString getNameInput() const;

    VSnippet::Type getTypeInput() const;

    QString getCursorMarkInput() const;

    QString getSelectionMarkInput() const;

    QString getContentInput() const;

    QChar getShortcutInput() const;

    bool getAutoIndentInput() const;

private slots:
    void handleInputChanged();

private:
    void setupUI(const QString &p_title, const QString &p_info);

    void setContentEditByType();

    QString getContentEditByType() const;

    QVector<QChar> getAvailableShortcuts() const;

    VMetaWordLineEdit *m_nameEdit;
    QComboBox *m_typeCB;
    QComboBox *m_shortcutCB;
    VLineEdit *m_cursorMarkEdit;
    VLineEdit *m_selectionMarkEdit;
    QCheckBox *m_autoIndentCB;
    QTextEdit *m_contentEdit;

    QLabel *m_warnLabel;
    QDialogButtonBox *m_btnBox;

    const QVector<VSnippet> &m_snippets;

    const VSnippet &m_snippet;
};

#endif // VEDITSNIPPETDIALOG_H
