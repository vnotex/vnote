var placeholder = document.getElementById('placeholder');

var updateHtml = function(html) {
    placeholder.innerHTML = html;
    var codes = document.getElementsByTagName('code');
    for (var i = 0; i < codes.length; ++i) {
        if (codes[i].parentElement.tagName.toLowerCase() == 'pre') {
            hljs.highlightBlock(codes[i]);
        }
    }
}

