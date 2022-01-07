/* Main script file for MarkdownViewer. */

new QWebChannel(qt.webChannelTransport,
    function(p_channel) {
        let adapter = p_channel.objects.vxAdapter;
        // Export the adapter globally.
        window.vxMarkdownAdapter = adapter;

        // Connect signals from CPP side.
        adapter.textUpdated.connect(function(p_text) {
            window.vnotex.setMarkdownText(p_text);
        });

        adapter.editLineNumberUpdated.connect(function(p_lineNumber) {
            window.vnotex.scrollToLine(p_lineNumber);
        });

        adapter.anchorScrollRequested.connect(function(p_anchor) {
            window.vnotex.scrollToAnchor(p_anchor);
        });

        adapter.graphPreviewRequested.connect(function(p_id, p_timeStamp, p_lang, p_text) {
            window.vnotex.previewGraph(p_id, p_timeStamp, p_lang, p_text);
        });

        adapter.mathPreviewRequested.connect(function(p_id, p_timeStamp, p_text) {
            window.vnotex.previewMath(p_id, p_timeStamp, p_text);
        });

        adapter.scrollRequested.connect(function(p_up) {
            window.vnotex.scroll(p_up);
        });

        adapter.htmlToMarkdownRequested.connect(function(p_id, p_timeStamp, p_html) {
            window.vnotex.htmlToMarkdown(p_id, p_timeStamp, p_html);
        });

        adapter.highlightCodeBlockRequested.connect(function(p_idx, p_timeStamp, p_text) {
            window.vnotex.highlightCodeBlock(p_idx, p_timeStamp, p_text);
        });

        adapter.parseStyleSheetRequested.connect(function(p_id, p_styleSheet) {
            window.vnotex.parseStyleSheet(p_id, p_styleSheet);
        });

        adapter.crossCopyRequested.connect(function(p_id, p_timeStamp, p_target, p_baseUrl, p_html) {
            window.vnotex.crossCopy(p_id, p_timeStamp, p_target, p_baseUrl, p_html);
        });

        adapter.findTextRequested.connect(function(p_texts, p_options, p_currentMatchLine) {
            window.vnotex.findText(p_texts, p_options, p_currentMatchLine);
        });

        adapter.contentRequested.connect(function() {
            window.vnotex.saveContent();
        });

        adapter.graphRenderDataReady.connect(function(p_id, p_index, p_format, p_data) {
            window.vnotex.graphRenderDataReady(p_id, p_index, p_format, p_data);
        });

        console.log('QWebChannel has been set up');
        if (window.vnotex.initialized) {
            window.vnotex.kickOffMarkdown();
        }
    });

window.vnotex.on('ready', function() {
    if (window.vxMarkdownAdapter) {
        window.vnotex.kickOffMarkdown();
    }
});
