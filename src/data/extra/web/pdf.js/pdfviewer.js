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

        console.log('QWebChannel has been set up');

        if (window.vxcore.initialized) {
            window.vxAdapter.setReady(true);
        }
    });

pdfjsLib.GlobalWorkerOptions.workerSrc = window.vxcore.workerSrc;

window.vxcore.on('ready', function() {
    if (window.vxAdapter) {
        window.vxAdapter.setReady(true);
    }
});
