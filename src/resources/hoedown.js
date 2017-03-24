var placeholder = document.getElementById('placeholder');

var updateHtml = function(html) {
    placeholder.innerHTML = html;
    var codes = document.getElementsByTagName('code');
    var mermaidIdx = 0;
    for (var i = 0; i < codes.length; ++i) {
        var code = codes[i];
        if (code.parentElement.tagName.toLowerCase() == 'pre') {
            if (VEnableMermaid && code.classList.contains('language-mermaid')) {
                // Mermaid code block.
                try {
                    var graph = mermaidAPI.render('mermaid-diagram-' + mermaidIdx++, code.innerText, function(){});
                } catch (err) {
                    content.setLog("err: " + err);
                    continue;
                }
                var graphDiv = document.createElement('div');
                graphDiv.classList.add(VMermaidDivClass);
                graphDiv.innerHTML = graph;
                var preNode = code.parentNode;
                preNode.classList.add(VMermaidDivClass);
                preNode.replaceChild(graphDiv, code);
                // replaceChild() will decrease codes.length.
                --i;
            } else {
                hljs.highlightBlock(code);
            }
        }
    }

    if (VEnableMathjax) {
        try {
            MathJax.Hub.Queue(["Typeset", MathJax.Hub, placeholder]);
        } catch (err) {
            content.setLog("err: " + err);
        }
    }
}

