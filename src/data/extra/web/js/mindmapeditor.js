/* Main script file for MindMapEditor. */

new QWebChannel(qt.webChannelTransport,
    function(p_channel) {
        let adapter = p_channel.objects.vxAdapter;
        // Export the adapter globally.
        window.vxAdapter = adapter;

        // Connect signals from CPP side.
        adapter.saveDataRequested.connect(function(p_id) {
            window.vxcore.saveData(p_id);
        });

        adapter.dataUpdated.connect(function(p_data) {
            window.vxcore.setData(p_data);
        });

        adapter.findTextRequested.connect(function(p_texts, p_options, p_currentMatchLine) {
            window.vxcore.findText(p_texts, p_options, p_currentMatchLine);
        });

        console.log('QWebChannel has been set up');

        if (window.vxcore.initialized) {
            window.vxAdapter.setReady(true);
        }
    });

window.vxcore.on('ready', function() {
    if (window.vxAdapter) {
        window.vxAdapter.setReady(true);
    }
});
