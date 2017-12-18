var placeholder = document.getElementById('placeholder');

// Use Marked to highlight code blocks in edit mode.
marked.setOptions({
    highlight: function(code, lang) {
        if (lang) {
            if (hljs.getLanguage(lang)) {
                return hljs.highlight(lang, code).value;
            } else {
                return hljs.highlightAuto(code).value;
            }
        } else {
            return code;
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
                if (renderMermaidOne(code)) {
                    // replaceChild() will decrease codes.length.
                    --i;
                    continue;
                }
            } else if (VEnableFlowchart && code.classList.contains('language-flowchart')) {
                // Flowchart code block.
                if (renderFlowchartOne(code)) {
                    // replaceChild() will decrease codes.length.
                    --i;
                    continue;
                }
            }

            if (listContainsRegex(code.classList, /language-.*/)) {
                hljs.highlightBlock(code);
            }
        }
    }

    addClassToCodeBlock();
    renderCodeBlockLineNumber();

    // If you add new logics after handling MathJax, please pay attention to
    // finishLoading logic.
    // MathJax may be not loaded for now.
    if (VEnableMathjax && (typeof MathJax != "undefined")) {
        try {
            MathJax.Hub.Queue(["Typeset", MathJax.Hub, placeholder, finishLogics]);
        } catch (err) {
            content.setLog("err: " + err);
            finishLogics();
        }
    } else {
        finishLogics();
    }
};

var highlightText = function(text, id, timeStamp) {
    var html = marked(text);
    content.highlightTextCB(html, id, timeStamp);
}
