var placeholder = document.getElementById('placeholder');

var scrollToAnchor = function(anchor) {
    var anc = document.getElementById(anchor);
    if (anc != null) {
        anc.scrollIntoView();
    }
};

var updateHtml = function(html) {
    placeholder.innerHTML = html;
    var codes = document.getElementsByTagName('code');
    for (var i = 0; i < codes.length; ++i) {
        if (codes[i].parentElement.tagName.toLowerCase() == 'pre') {
            hljs.highlightBlock(codes[i]);
        }
    }
}

var onWindowScroll = function() {
    var scrollTop = document.documentElement.scrollTop || document.body.scrollTop || window.pageYOffset;
    var eles = document.querySelectorAll("h1, h2, h3, h4, h5, h6");

    if (eles.length == 0) {
        return;
    }
    var curIdx = 0;
    var biaScrollTop = scrollTop + 50;
    for (var i = 0; i < eles.length; ++i) {
        if (biaScrollTop >= eles[i].offsetTop) {
            curIdx = i;
        } else {
            break;
        }
    }

    var curHeader = eles[curIdx].getAttribute("id");
    if (curHeader != null) {
        content.setHeader(curHeader);
    }
}
