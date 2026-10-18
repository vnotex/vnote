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

    window.vxcore.contentContainer.addEventListener('click', handleTaskListClick);
}

// The presentation root uses the same handler while the original nodes are relocated.
function handleTaskListClick(p_event) {
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
        window.vxWebJobs.bind(p_channel.objects.vxJobs);

        // Connect signals from CPP side.
        adapter.presentationModeRequested.connect(function(p_active, p_darkTheme) {
            if (window.vxPresentation) {
                window.vxPresentation.setActive(p_active, p_darkTheme);
            } else if (p_active) {
                adapter.setPresentationState(false, 'The presentation runtime is unavailable');
            }
        });

        adapter.textUpdated.connect(function(p_text) {
            window.vxcore.setMarkdownText(p_text);
        });

        adapter.editLineNumberUpdated.connect(function(p_lineNumber) {
            window.vxcore.scrollToLine(p_lineNumber);
        });

        adapter.anchorScrollRequested.connect(function(p_anchor) {
            window.vxcore.scrollToAnchor(p_anchor);
        });

        adapter.scrollRequested.connect(function(p_up) {
            window.vxcore.scroll(p_up);
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

        adapter.graphRenderDataReady.connect(function(p_id, p_index, p_format, p_data, p_success) {
            window.vxcore.graphRenderDataReady(p_id, p_index, p_format, p_data, p_success);
        });

        adapter.taskListToggleRejected.connect(function(p_lineNumber) {
            revertTaskListItem(p_lineNumber);
        });

        installImageViewHandler();
        console.log('QWebChannel has been set up');
        if (window.vxcore.initialized) {
            window.vxcore.kickOffMarkdown();
            installTaskListHandler();
        }
    });

function installImageViewHandler() {
    if (window.vxImageViewer) {
        window.vxImageViewer.bindAdapter(window.vxMarkdownAdapter);
    }
}

window.vxcore.on('ready', function() {
    installImageViewHandler();
    if (window.vxMarkdownAdapter) {
        window.vxcore.kickOffMarkdown();
        installTaskListHandler();
    }
});
