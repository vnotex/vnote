var channelInitialized = false;

var contentDiv = document.getElementById('content-div');

var content;

new QWebChannel(qt.webChannelTransport,
    function(channel) {
        content = channel.objects.content;

        content.requestPreviewMathJax.connect(previewMathJax);

        channelInitialized = true;
    });

var previewMathJax = function(identifier, id, text) {
    if (text.length == 0) {
        return;
    }

    var p = document.createElement('p');
    p.id = identifier + '_' + id;
    p.textContent = text;
    contentDiv.appendChild(p);

    try {
        MathJax.Hub.Queue(["Typeset",
                           MathJax.Hub,
                           p,
                           postProcessMathJax.bind(undefined, identifier, id, p)]);
    } catch (err) {
        console.log("err: " + err);
        contentDiv.removeChild(p);
        delete p;
    }
};

var postProcessMathJax = function(identifier, id, container) {
    domtoimage.toPng(container, { height: container.clientHeight * 1.5 }).then(function (dataUrl) {
        var png = dataUrl.substring(dataUrl.indexOf(',') + 1);
        content.mathjaxResultReady(identifier, id, 'png', png);

        contentDiv.removeChild(container);
        delete container;
    }).catch(function (err) {
        console.log("err: " + err);
    });
};
