/* Main script file for PdfViewer. */

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

        console.log('QWebChannel has been set up');

        if (window.vxcore.initialized) {
            window.vxAdapter.setReady(true);
        }
    });

pdfjsLib.GlobalWorkerOptions.workerSrc = window.vxcore.workerSrc;

// Keep the sidebar (thumbnails/outline) closed on open. VNote shows the PDF outline in its
// own side dock, so the built-in navigation pane is redundant by default.
// MUST run before viewer.js's webViewerLoad() (DOMContentLoaded), which this file does, since
// it is a synchronous script in <head>.
// - sidebarViewOnLoad = SidebarView.NONE (0). The default -1 (UNKNOWN) makes pdf.js fall back
//   to the stored state or to the document's own /PageMode, which can auto-open the sidebar.
// - disablePreferences is required: otherwise AppOptions.setAll(preferences) in
//   _initializeOptions() overwrites the value above with the -1 default.
window.PDFViewerApplicationOptions.set('disablePreferences', true);
window.PDFViewerApplicationOptions.set('sidebarViewOnLoad', 0);

// initializedPromise resolves after PDFViewerApplication.eventBus exists, which
// it does NOT at the top level of this file. Register from there — NOT from the
// QWebChannel callback above, which would invert the race and can miss
// 'documentloaded' entirely.
window.PDFViewerApplication.initializedPromise.then(function() {
    window.vxcore.attachOutlineBridge(window.PDFViewerApplication);
});

window.vxcore.on('ready', function() {
    if (window.vxAdapter) {
        window.vxAdapter.setReady(true);
    }
});
