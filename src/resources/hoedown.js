var VRenderer = 'hoedown';

// Use Marked to highlight code blocks in edit mode.
marked.setOptions({
    highlight: function(code, lang) {
        if (lang) {
            if (lang === 'wavedrom') {
                lang = 'json';
            }

            if (hljs.getLanguage(lang)) {
                return hljs.highlight(lang, code, true).value;
            } else {
                return hljs.highlightAuto(code).value;
            }
        } else {
            return code;
        }
    }
});

var updateHtml = function(html) {
    startFreshRender();

    // There is at least one async job for MathJax.
    asyncJobsCount = 1;

    contentDiv.innerHTML = html;

    insertImageCaption();
    setupImageView();

    var codes = document.getElementsByTagName('code');
    mermaidIdx = 0;
    flowchartIdx = 0;
    wavedromIdx = 0;
    plantUMLIdx = 0;
    graphvizIdx = 0;
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
            } else if (VEnableFlowchart
                       && (code.classList.contains('language-flowchart')
                           || code.classList.contains('language-flow'))) {
                // Flowchart code block.
                if (renderFlowchartOne(code)) {
                    // replaceChild() will decrease codes.length.
                    --i;
                    continue;
                }
            } else if (VEnableWavedrom && code.classList.contains('language-wavedrom')) {
                // Wavedrom code block.
                if (renderWavedromOne(code)) {
                    // replaceChild() will decrease codes.length.
                    --i;
                    continue;
                }
            } else if (VEnableMathjax && code.classList.contains('language-mathjax')) {
                // Mathjax code block.
                continue;
            } else if (VPlantUMLMode != 0
                       && code.classList.contains('language-puml')) {
                // PlantUML code block.
                if (VPlantUMLMode == 1) {
                    if (renderPlantUMLOneOnline(code)) {
                        // replaceChild() will decrease codes.length.
                        --i;
                    }
                } else {
                    renderPlantUMLOneLocal(code);
                }

                continue;
            } else if (VEnableGraphviz
                       && code.classList.contains('language-dot')) {
                // Graphviz code block.
                renderGraphvizOneLocal(code);
                continue;
            }


            if (listContainsRegex(code.classList, /language-.*/)) {
                hljs.highlightBlock(code);
            }
        }
    }

    addClassToCodeBlock();
    addCopyButtonToCodeBlock();
    renderCodeBlockLineNumber();

    // If you add new logics after handling MathJax, please pay attention to
    // finishLoading logic.
    // MathJax may be not loaded for now.
    if (VEnableMathjax && (typeof MathJax != "undefined")) {
        MathJax.texReset();
        MathJax
            .typesetPromise([contentDiv])
            .then(postProcessMathJax)
            .catch(function (err) {
                content.setLog("err: " + err);
                finishOneAsyncJob();
            });
    } else {
        finishOneAsyncJob();
    }
};

var highlightText = function(text, id, timeStamp) {
    var html = marked(text);
    content.highlightTextCB(html, id, timeStamp);
}

var textToHtml = function(identifier, id, timeStamp, text, inlineStyle) {
    var html = marked(text);
    if (inlineStyle) {
        var container = textHtmlDiv;
        container.innerHTML = html;
        html = getHtmlWithInlineStyles(container);
        container.innerHTML = "";
    }

    content.textToHtmlCB(identifier, id, timeStamp, html);
}
