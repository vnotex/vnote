#ifndef SNIPPETINFOWIDGET_H
#define SNIPPETINFOWIDGET_H

#include <QWidget>

#include <snippet/snippet.h>

class QLineEdit;
class QComboBox;
class QCheckBox;
class QPlainTextEdit;

namespace vnotex
{
    class SnippetInfoWidget : public QWidget
    {
        Q_OBJECT
    public:
        enum Mode { Create, Edit };

        explicit SnippetInfoWidget(QWidget *p_parent = nullptr);

        SnippetInfoWidget(const Snippet *p_snippet, QWidget *p_parent = nullptr);

        QString getName() const;

        Snippet::Type getType() const;

        int getShortcut() const;

        QString getCursorMark() const;

        QString getSelectionMark() const;

        bool shouldIndentAsFirstLine() const;

        QString getContent() const;

        QString getDescription() const;

    signals:
        void inputEdited();

    private:
        void setupUI();

        void setupTypeComboBox(QWidget *p_parent);

        void setupShortcutComboBox(QWidget *p_parent);

        void setSnippet(const Snippet *p_snippet);

        void initShortcutComboBox();

        Mode m_mode = Mode::Create;

        const Snippet *m_snippet = nullptr;

        QLineEdit *m_nameLineEdit = nullptr;

        QLineEdit *m_descriptionLineEdit = nullptr;

        QComboBox *m_typeComboBox = nullptr;

        QComboBox *m_shortcutComboBox = nullptr;

        QLineEdit *m_cursorMarkLineEdit = nullptr;

        QLineEdit *m_selectionMarkLineEdit = nullptr;

        QCheckBox *m_indentAsFirstLineCheckBox = nullptr;

        QPlainTextEdit *m_contentTextEdit = nullptr;
    };
}

#endif // SNIPPETINFOWIDGET_H
