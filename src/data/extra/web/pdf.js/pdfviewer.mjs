/* Main script file for PdfViewer.
 *
 * === Load-order contract (pdf.js v6, ESM) ===
 *
 * This file is loaded with type="module" and is therefore DEFERRED: it runs
 * after the whole document has been parsed and after every earlier module
 * script (build/pdf.mjs, web/viewer.mjs) has executed.
 *
 * That gives it `QWebChannel`, `window.vxcore` (from the classic scripts) and
 * `window.PDFViewerApplication` (from viewer.mjs).
 *
 * It does NOT give it a chance to configure pdf.js. A deferred script runs when
 * `document.readyState` is already "interactive", and viewer.mjs ends with
 * `if (readyState === "interactive" || "complete") webViewerLoad()`, so
 * `PDFViewerApplication.run()` has ALREADY happened by the time this file
 * executes. Every AppOption must therefore be set from the 'webviewerloaded'
 * listener in pdfviewercore.js, which is a classic script and runs first.
 * Setting an option here appears to work — the value lands in the options
 * object — but pdf.js has already read it and nothing consumes it.
 *
 * What IS safe here is anything keyed off `initializedPromise` or the event bus,
 * which resolve later.
 *
 * It contains no `import`/`export` on purpose: `tests/widgets/test_pdfviewercore_js.cpp`
 * evaluates this exact file with QJSEngine, which has no module loader.
 * `type="module"` is used for its DEFERRAL, not for its module graph.
 */

new QWebChannel(qt.webChannelTransport,
    function(p_channel) {
        let adapter = p_channel.objects.vxAdapter;
        // Export the adapter globally.
        window.vxAdapter = adapter;

        // Connect signals from CPP side.
        adapter.urlUpdated.connect(function(p_url) {
            window.vxcore.loadPdf(p_url);
        });

        adapter.outlineItemScrollRequested.connect(function(p_index) {
            window.vxcore.gotoOutlineItem(p_index);
        });

        // MUST be unconditional: this is one of the two inputs of the publish
        // rendezvous in pdfviewercore.js, and the outline may already be waiting.
        window.vxcore.setOutlineAdapter(adapter);

        // Comments. Same unconditional rule, for the same reason: C++ may have
        // already published a set for this document.
        window.vxcore.setCommentAdapter(adapter);

        adapter.commentsUpdated.connect(function(p_comments) {
            window.vxcore.setComments(p_comments);
        });

        adapter.commentScrollRequested.connect(function(p_id) {
            window.vxcore.scrollToComment(p_id);
        });

        // The inline free-text editor. Driven from C++ right after a box is
        // placed, so the Text tool writes where the user clicked instead of
        // leaving an empty placeholder behind.
        adapter.commentTextEditRequested.connect(function(p_id) {
            window.vxcore.beginFreeTextEdit(p_id, true);
        });

        adapter.commentsEditableChanged.connect(function(p_editable) {
            window.vxcore.setCommentsEditable(p_editable);
        });

        // Driven by the page context menu on the C++ side.
        adapter.captureSelectionRequested.connect(function(p_color) {
            window.vxcore.setCommentColor(p_color);
            window.vxcore.captureSelection();
        });

        adapter.toolOptionsChanged.connect(function(p_tool, p_options) {
            window.vxcore.setToolOptions(p_tool, p_options);
        });

        adapter.toolChanged.connect(function(p_tool) {
            window.vxcore.setTool(p_tool);
        });

        console.log('QWebChannel has been set up');

        if (window.vxcore.initialized) {
            window.vxAdapter.setReady(true);
        }
    });

// NOTE: AppOptions are deliberately NOT set here. A deferred module runs after
// readyState is already "interactive", so viewer.mjs has ALREADY called
// webViewerLoad() -> PDFViewerApplication.run() by the time this file executes,
// and anything written into AppOptions now is set after pdf.js has read it.
// They are applied from the 'webviewerloaded' listener in pdfviewercore.js
// (a classic script) instead -- see the long comment at the bottom of that file.
// initializedPromise resolves after PDFViewerApplication.eventBus exists, which
// it does NOT at the top level of this file. Register from there — NOT from the
// QWebChannel callback above, which would invert the race and can miss
// 'documentloaded' entirely.
window.PDFViewerApplication.initializedPromise.then(function() {
    window.vxcore.attachOutlineBridge(window.PDFViewerApplication);
    window.vxcore.attachCommentBridge(window.PDFViewerApplication);

    // Highlight-on-selection. Bound to mouseup on the viewer container rather
    // than to 'selectionchange', which fires continuously while dragging and
    // would create a comment per intermediate selection.
    //
    // Gated on Alt: a bare selection must keep meaning "copy this text". Alt is
    // used because Ctrl/Shift are already selection modifiers and Alt+drag is
    // not bound by pdf.js.
    const container = document.getElementById('viewerContainer');
    if (!container) {
        return;
    }

    // === Highlight ===
    //
    // Bound to mouseup rather than 'selectionchange', which fires continuously
    // while dragging and would create a comment per intermediate selection.
    //
    // Two ways in: the Highlight TOOL (armed from the toolbar, the cheap path --
    // every selection is captured), or Alt as a one-off shortcut while in
    // reading mode, so a bare selection still means "copy this text".
    container.addEventListener('mouseup', function(p_event) {
        const tool = window.vxcore.tool;
        if (tool === 'highlight' || (tool === 'none' && p_event.altKey)) {
            window.vxcore.captureSelection();
        }
    });

    // === Ink and Text ===
    //
    // Pointer events, not mouse events, so a pen or touch drag works too, and
    // setPointerCapture keeps the stroke alive when the cursor leaves the page.
    container.addEventListener('pointerdown', function(p_event) {
        if (window.vxcore.tool === 'ink') {
            if (window.vxcore.beginInk(p_event.clientX, p_event.clientY, p_event.pointerId)) {
                // Suppress the text-layer drag that would otherwise start a
                // selection under the stroke.
                p_event.preventDefault();
                container.setPointerCapture(p_event.pointerId);
            }
        } else if (window.vxcore.tool === 'freetext') {
            // placeFreeText() disarms the tool itself, so the one-shot rule
            // lives with the behaviour rather than only in this handler.
            if (window.vxcore.placeFreeText(p_event.clientX, p_event.clientY)) {
                p_event.preventDefault();
            }
        }
    });

    container.addEventListener('pointermove', function(p_event) {
        if (window.vxcore.tool === 'ink' && window.vxcore.inkDraft) {
            p_event.preventDefault();
            window.vxcore.extendInk(p_event.clientX, p_event.clientY, p_event.pointerId);
        }
    });

    const releaseCapture = function(p_event) {
        if (container.hasPointerCapture(p_event.pointerId)) {
            container.releasePointerCapture(p_event.pointerId);
        }
    };

    // Only a matching pointerup COMMITS.
    container.addEventListener('pointerup', function(p_event) {
        if (window.vxcore.inkDraft) {
            window.vxcore.endInk(p_event.pointerId);
        }
        releaseCapture(p_event);
    });

    // A cancelled gesture did NOT complete: discard it. Committing here would
    // save a stroke the user aborted (palm rejection, a system gesture taking
    // over, the window losing the pointer).
    const discard = function(p_event) {
        if (window.vxcore.ownsPointer(p_event.pointerId)) {
            window.vxcore.abortInk();
        }
        releaseCapture(p_event);
    };
    container.addEventListener('pointercancel', discard);
    container.addEventListener('lostpointercapture', discard);

    // Esc leaves any authoring tool. Without it an armed tool can only be
    // cleared from the toolbar, which is a long way from the page. The
    // free-text editor handles its OWN Esc (and stops propagation), so this is
    // only the fallback for a box that somehow lost focus while still open.
    window.addEventListener('keydown', function(p_event) {
        if (p_event.key !== 'Escape') {
            return;
        }
        if (window.vxcore.editingCommentId) {
            p_event.preventDefault();
            window.vxcore.cancelFreeTextEdit();
            return;
        }
        if (window.vxcore.tool !== 'none') {
            p_event.preventDefault();
            window.vxcore.finishTool();
        }
    });
});

window.vxcore.on('ready', function() {
    if (window.vxAdapter) {
        window.vxAdapter.setReady(true);
    }
});

// Last-chance flush of an in-progress free-text body.
//
// Typing is streamed on a debounce (see pdfviewercore.js), so up to one
// debounce window of text exists only inside the contenteditable. 'pagehide'
// and a hidden 'visibilitychange' are the page-lifecycle events that fire
// before a navigation, a reload or the window going away, and they are the last
// point at which the QWebChannel is still usable.
//
// This is a NARROWING, not a guarantee: nothing running in the page can survive
// the render process simply being killed. 'documenterror' / 'pagesdestroy' are
// covered separately, inside resetComments().
if (typeof window.addEventListener === 'function') {
    const flushFreeText = function() {
        if (window.vxcore) {
            window.vxcore.flushFreeTextDraft();
        }
    };
    window.addEventListener('pagehide', flushFreeText);
    document.addEventListener('visibilitychange', function() {
        if (document.visibilityState === 'hidden') {
            flushFreeText();
        }
    });
}
