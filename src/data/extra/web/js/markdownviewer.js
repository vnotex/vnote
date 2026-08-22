/* Main script file for MarkdownViewer. */

window.vxTaskListHandlerInstalled = false;

// Install a delegated click handler for task list checkboxes.
// Idempotent: safe to call from either init order.
function installTaskListHandler() {
    if (window.vxTaskListHandlerInstalled) {
        return;
    }
    if (!window.vxMarkdownAdapter || !window.vxcore || !window.vxcore.contentContainer) {
        return;
    }
    window.vxTaskListHandlerInstalled = true;

    window.vxcore.contentContainer.addEventListener('click', function(p_event) {
        let target = p_event.target;
        if (!target || target.tagName !== 'INPUT'
            || !target.classList.contains('task-list-item-checkbox')) {
            return;
        }

        let item = target.closest('[data-source-line]');
        if (!item) {
            // No source mapping, revert the optimistic toggle.
            target.checked = !target.checked;
            return;
        }

        let lineNumber = parseInt(item.getAttribute('data-source-line'), 10);
        if (isNaN(lineNumber)) {
            target.checked = !target.checked;
            return;
        }

        window.vxMarkdownAdapter.toggleTaskListItem(lineNumber, target.checked);
    });
}

function revertTaskListItem(p_lineNumber) {
    let selector = '[data-source-line="' + p_lineNumber + '"] input.task-list-item-checkbox';
    let checkbox = document.querySelector(selector);
    if (checkbox) {
        checkbox.checked = !checkbox.checked;
    }
}

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

        adapter.graphPreviewRequested.connect(function(p_id, p_timeStamp, p_lang, p_text, p_scale = 1) {
            window.vxcore.previewGraph(p_id, p_timeStamp, p_lang, p_text, p_scale);
        });

        adapter.mathPreviewRequested.connect(function(p_id, p_timeStamp, p_text, p_scale = 1) {
            window.vxcore.previewMath(p_id, p_timeStamp, p_text, p_scale);
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

        adapter.highlightMathRequested.connect(function(p_idx, p_timeStamp, p_text) {
            window.vxcore.highlightMath(p_idx, p_timeStamp, p_text);
        });

        adapter.parseStyleSheetRequested.connect(function(p_id, p_styleSheet) {
            window.vxcore.parseStyleSheet(p_id, p_styleSheet);
        });

        adapter.headingAnchorRequested.connect(function(p_id, p_text, p_lineNumber) {
            window.vxcore.getHeadingAnchor(p_id, p_text, p_lineNumber);
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

        adapter.taskListToggleRejected.connect(function(p_lineNumber) {
            revertTaskListItem(p_lineNumber);
        });

        console.log('QWebChannel has been set up');
        if (window.vxcore.initialized) {
            window.vxcore.kickOffMarkdown();
            installTaskListHandler();
        }
    });

window.vxcore.on('ready', function() {
    if (window.vxMarkdownAdapter) {
        window.vxcore.kickOffMarkdown();
        installTaskListHandler();
    }
});
