#ifndef MINDMAPVIEWWINDOW_H
#define MINDMAPVIEWWINDOW_H

#include "viewwindow.h"

#include <QScopedPointer>

class QWebEngineView;

namespace vnotex
{
    class MindMapEditor;
    class MindMapEditorAdapter;

    class MindMapViewWindow : public ViewWindow
    {
        Q_OBJECT
    public:
        explicit MindMapViewWindow(QWidget *p_parent = nullptr);

        QString getLatestContent() const Q_DECL_OVERRIDE;

        QString selectedText() const Q_DECL_OVERRIDE;

        void setMode(ViewWindowMode p_mode) Q_DECL_OVERRIDE;

        void openTwice(const QSharedPointer<FileOpenParameters> &p_paras) Q_DECL_OVERRIDE;

        ViewWindowSession saveSession() const Q_DECL_OVERRIDE;

        void applySnippet(const QString &p_name) Q_DECL_OVERRIDE;

        void applySnippet() Q_DECL_OVERRIDE;

        void fetchWordCountInfo(const std::function<void(const WordCountInfo &)> &p_callback) const Q_DECL_OVERRIDE;

    public slots:
        void handleEditorConfigChange() Q_DECL_OVERRIDE;

    protected slots:
        void setModified(bool p_modified) Q_DECL_OVERRIDE;

        void print() Q_DECL_OVERRIDE;

        void toggleDebug() Q_DECL_OVERRIDE;

        void handleFindTextChanged(const QString &p_text, FindOptions p_options) Q_DECL_OVERRIDE;

        void handleFindNext(const QStringList &p_texts, FindOptions p_options) Q_DECL_OVERRIDE;

        void handleReplace(const QString &p_text, FindOptions p_options, const QString &p_replaceText) Q_DECL_OVERRIDE;

        void handleReplaceAll(const QString &p_text, FindOptions p_options, const QString &p_replaceText) Q_DECL_OVERRIDE;

        void handleFindAndReplaceWidgetClosed() Q_DECL_OVERRIDE;

        void showFindAndReplaceWidget() Q_DECL_OVERRIDE;

    protected:
        void syncEditorFromBuffer() Q_DECL_OVERRIDE;

        void syncEditorFromBufferContent() Q_DECL_OVERRIDE;

        void scrollUp() Q_DECL_OVERRIDE;

        void scrollDown() Q_DECL_OVERRIDE;

        void zoom(bool p_zoomIn) Q_DECL_OVERRIDE;

    private:
        void setupUI();

        void setupToolBar();

        void setupEditor();

        MindMapEditorAdapter *adapter() const;

        bool updateConfigRevision();

        void setupDebugViewer();

        // Managed by QObject.
        MindMapEditor *m_editor = nullptr;

        // Used to debug web view.
        QWebEngineView *m_debugViewer = nullptr;

        int m_editorConfigRevision = 0;
    };
}

#endif // MINDMAPVIEWWINDOW_H
