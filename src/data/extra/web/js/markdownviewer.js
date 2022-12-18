/* Main script file for MarkdownViewer. */

new QWebChannel(qt.webChannelTransport,
    function(p_channel) {
        let adapter = p_channel.objects.vxAdapter;
        // Export the adapter globally.
        window.vxMarkdownAdapter = adapter;

        // Connect signals from CPP side.
        adapter.textUpdated.connect(function(p_text) {
            window.vxcore.setMarkdownText(p_text);
        });

        adapter.editLineNumberUpdated.connect(function(p_lineNumber) {
            window.vxcore.scrollToLine(p_lineNumber);
        });

        adapter.anchorScrollRequested.connect(function(p_anchor) {
            window.vxcore.scrollToAnchor(p_anchor);
        });

        adapter.graphPreviewRequested.connect(function(p_id, p_timeStamp, p_lang, p_text) {
            window.vxcore.previewGraph(p_id, p_timeStamp, p_lang, p_text);
        });

        adapter.mathPreviewRequested.connect(function(p_id, p_timeStamp, p_text) {
            window.vxcore.previewMath(p_id, p_timeStamp, p_text);
        });

        adapter.scrollRequested.connect(function(p_up) {
            window.vxcore.scroll(p_up);
        });

        adapter.htmlToMarkdownRequested.connect(function(p_id, p_timeStamp, p_html) {
            window.vxcore.htmlToMarkdown(p_id, p_timeStamp, p_html);
        });

        adapter.highlightCodeBlockRequested.connect(function(p_idx, p_timeStamp, p_text) {
            window.vxcore.highlightCodeBlock(p_idx, p_timeStamp, p_text);
        });

        adapter.parseStyleSheetRequested.connect(function(p_id, p_styleSheet) {
            window.vxcore.parseStyleSheet(p_id, p_styleSheet);
        });

        adapter.crossCopyRequested.connect(function(p_id, p_timeStamp, p_target, p_baseUrl, p_html) {
            window.vxcore.crossCopy(p_id, p_timeStamp, p_target, p_baseUrl, p_html);
        });

        adapter.findTextRequested.connect(function(p_texts, p_options, p_currentMatchLine) {
            window.vxcore.findText(p_texts, p_options, p_currentMatchLine);
        });

        adapter.contentRequested.connect(function() {
            window.vxcore.saveContent();
        });

        adapter.graphRenderDataReady.connect(function(p_id, p_index, p_format, p_data) {
            window.vxcore.graphRenderDataReady(p_id, p_index, p_format, p_data);
        });

        console.log('QWebChannel has been set up');
        if (window.vxcore.initialized) {
            window.vxcore.kickOffMarkdown();
        }
    });

window.vxcore.on('ready', function() {
    if (window.vxMarkdownAdapter) {
        window.vxcore.kickOffMarkdown();
    }
});
