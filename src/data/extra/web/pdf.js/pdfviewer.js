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
