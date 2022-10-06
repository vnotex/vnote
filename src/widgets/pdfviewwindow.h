#ifndef PDFVIEWWINDOW_H
#define PDFVIEWWINDOW_H

#include "viewwindow.h"

#include <QScopedPointer>

class QSplitter;
class QStackedWidget;
class QWebEngineView;
class QActionGroup;
class QTimer;

namespace vte
{
    class MarkdownEditorConfig;
    struct TextEditorParameters;
}

namespace vnotex
{
    class PdfViewWindow : public ViewWindow
    {
        Q_OBJECT
    public:
        PdfViewWindow(QWidget *p_parent = nullptr);

        ~PdfViewWindow();

        QString getLatestContent() const Q_DECL_OVERRIDE;

        QString selectedText() const Q_DECL_OVERRIDE;

        void setMode(ViewWindowMode p_mode) Q_DECL_OVERRIDE;

        QSharedPointer<OutlineProvider> getOutlineProvider() Q_DECL_OVERRIDE;

        void openTwice(const QSharedPointer<FileOpenParameters> &p_paras) Q_DECL_OVERRIDE;

        ViewWindowSession saveSession() const Q_DECL_OVERRIDE;

        void applySnippet(const QString &p_name) Q_DECL_OVERRIDE;

        void applySnippet() Q_DECL_OVERRIDE;

        void fetchWordCountInfo(const std::function<void(const WordCountInfo &)> &p_callback) const Q_DECL_OVERRIDE;

    public slots:
        void handleEditorConfigChange() Q_DECL_OVERRIDE;

    protected slots:
        void setModified(bool p_modified) Q_DECL_OVERRIDE;

        void handleBufferChangedInternal(const QSharedPointer<FileOpenParameters> &p_paras) Q_DECL_OVERRIDE;

        void handleTypeAction(TypeAction p_action) Q_DECL_OVERRIDE;

        void handleSectionNumberOverride(OverrideState p_state) Q_DECL_OVERRIDE;

        void handleImageHostChanged(const QString &p_hostName) Q_DECL_OVERRIDE;

        void handleFindTextChanged(const QString &p_text, FindOptions p_options) Q_DECL_OVERRIDE;

        void handleFindNext(const QStringList &p_texts, FindOptions p_options) Q_DECL_OVERRIDE;

        void handleReplace(const QString &p_text, FindOptions p_options, const QString &p_replaceText) Q_DECL_OVERRIDE;

        void handleReplaceAll(const QString &p_text, FindOptions p_options, const QString &p_replaceText) Q_DECL_OVERRIDE;

        void handleFindAndReplaceWidgetClosed() Q_DECL_OVERRIDE;

        void handleFindAndReplaceWidgetOpened() Q_DECL_OVERRIDE;

        void toggleDebug() Q_DECL_OVERRIDE;

        void print() Q_DECL_OVERRIDE;

        void clearHighlights() Q_DECL_OVERRIDE;

    protected:
        void syncEditorFromBuffer() Q_DECL_OVERRIDE;

        void syncEditorFromBufferContent() Q_DECL_OVERRIDE;

        bool eventFilter(QObject *p_obj, QEvent *p_event) Q_DECL_OVERRIDE;

        void detachFromBufferInternal() Q_DECL_OVERRIDE;

        void scrollUp() Q_DECL_OVERRIDE;

        void scrollDown() Q_DECL_OVERRIDE;

        void zoom(bool p_zoomIn) Q_DECL_OVERRIDE;

        QPoint getFloatingWidgetPosition() Q_DECL_OVERRIDE;

        void updateViewModeMenu(QMenu *p_menu) Q_DECL_OVERRIDE;

    private:
        void setupUI();
    };
}


#endif // PDFVIEWWINDOW_H
