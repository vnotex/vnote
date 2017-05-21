var placeholder = document.getElementById('placeholder');

// Use Marked to highlight code blocks in edit mode.
marked.setOptions({
    highlight: function(code, lang) {
        if (lang && hljs.getLanguage(lang)) {
            return hljs.highlight(lang, code).value;
        } else {
            return hljs.highlightAuto(code).value;
        }
    }
});

var updateHtml = function(html) {
    placeholder.innerHTML = html;

    insertImageCaption();

    var codes = document.getElementsByTagName('code');
    mermaidIdx = 0;
    for (var i = 0; i < codes.length; ++i) {
        var code = codes[i];
        if (code.parentElement.tagName.toLowerCase() == 'pre') {
            if (VEnableMermaid && code.classList.contains('language-mermaid')) {
                // Mermaid code block.
                mermaidParserErr = false;
                mermaidIdx++;
                try {
                    // Do not increment mermaidIdx here.
                    var graph = mermaidAPI.render('mermaid-diagram-' + mermaidIdx, code.innerText, function(){});
                } catch (err) {
                    content.setLog("err: " + err);
                    continue;
                }
                if (mermaidParserErr || typeof graph == "undefined") {
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

    // If you add new logics after handling MathJax, please pay attention to
    // finishLoading logic.
    // MathJax may be not loaded for now.
    if (VEnableMathjax && (typeof MathJax != "undefined")) {
        try {
            MathJax.Hub.Queue(["Typeset", MathJax.Hub, placeholder, finishLoading]);
        } catch (err) {
            content.setLog("err: " + err);
            finishLoading();
        }
    } else {
        finishLoading();
    }
};

var highlightText = function(text, id, timeStamp) {
    var html = marked(text);
    content.highlightTextCB(html, id, timeStamp);
}

