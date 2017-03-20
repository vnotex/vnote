var content;
var keyState = 0;

new QWebChannel(qt.webChannelTransport,
    function(channel) {
        content = channel.objects.content;
        if (typeof updateHtml == "function") {
            updateHtml(content.html);
            content.htmlChanged.connect(updateHtml);
        }
        if (typeof updateText == "function") {
            content.textChanged.connect(updateText);
            content.updateText();
        }
        content.requestScrollToAnchor.connect(scrollToAnchor);
    });

window.onscroll = onWindowScroll;

document.onkeydown = function(e) {
    e = e || window.event;
    var key;
    var shift;
    var ctrl;
    if (e.which) {
        key = e.which;
    } else {
        key = e.keyCode;
    }
    shift = !!e.shiftKey;
    ctrl = !!e.ctrlKey;
    switch (key) {
    case 74: // J
        window.scrollBy(0, 100);
        keyState = 0;
    break;

    case 75: // K
        window.scrollBy(0, -100);
        keyState = 0;
    break;

    case 72: // H
        window.scrollBy(-100, 0);
        keyState = 0;
    break;

    case 76: // L
        window.scrollBy(100, 0);
        keyState = 0;
    break;

    case 71: // G
        if (shift) {
            var scrollLeft = document.documentElement.scrollLeft || document.body.scrollLeft || window.pageXOffset;
            var scrollHeight = document.documentElement.scrollHeight || document.body.scrollHeight;
            window.scrollTo(scrollLeft, scrollHeight);
            keyState = 0;
            break;
        } else {
            if (keyState == 0) {
                keyState = 1;
            } else if (keyState == 1) {
                keyState = 0;
                var scrollLeft = document.documentElement.scrollLeft || document.body.scrollLeft || window.pageXOffset;
                window.scrollTo(scrollLeft, 0);
            }
            break;
        }
        return;

    case 85: // U
        keyState = 0;
        if (ctrl) {
            var clientHeight = document.documentElement.clientHeight;
            window.scrollBy(0, -clientHeight);
            break;
        }
        return;

    case 68: // D
        keyState = 0;
        if (ctrl) {
            var clientHeight = document.documentElement.clientHeight;
            window.scrollBy(0, clientHeight);
            break;
        }
        return;

    default:
        content.keyPressEvent(key);
        keyState = 0;
        return;
    }
    e.preventDefault();
}
