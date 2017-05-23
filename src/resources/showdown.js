var placeholder = document.getElementById('placeholder');
var renderer = new showdown.Converter({simplifiedAutoLink: 'true',
                                       excludeTrailingPunctuationFromURLs: 'true',
                                       strikethrough: 'true',
                                       tables: 'true',
                                       tasklists: 'true',
                                       literalMidWordUnderscores: 'true',
                                       extensions: ['headinganchor']
                                       });

var toc = []; // Table of contents as a list

// Parse the html's headings and construct toc[].
var parseHeadings = function(html) {
    toc = [];
    var parser = new DOMParser();
    var htmlDoc = parser.parseFromString(html, 'text/html');
    var eles = htmlDoc.querySelectorAll("h1, h2, h3, h4, h5, h6");

    for (var i = 0; i < eles.length; ++i) {
        var ele = eles[i];
        var level = parseInt(ele.tagName.substr(1));

        toc.push({
            level: level,
            anchor: ele.id,
            title: ele.innerText
        });
    }

    delete parser;
};

var markdownToHtml = function(markdown, needToc) {
    var html = renderer.makeHtml(markdown);

    // Parse the html to init toc[].
    parseHeadings(html);

    if (needToc) {
        return html.replace(/<p>\[TOC\]<\/p>/ig, '<div class="vnote-toc"></div>');
    } else {
        return html;
    }
};

// Handle wrong title levels, such as '#' followed by '###'
var toPerfectToc = function(toc) {
    var i;
    var curLevel = 1;
    var perfToc = [];
    for (i in toc) {
        var item = toc[i];
        while (item.level > curLevel + 1) {
            curLevel += 1;
            var tmp = { level: curLevel,
                        anchor: item.anchor,
                        title: '[EMPTY]'
                      };
            perfToc.push(tmp);
        }
        perfToc.push(item);
        curLevel = item.level;
    }
    return perfToc;
};

var itemToHtml = function(item) {
    return '<a href="#' + item.anchor + '">' + item.title + '</a>';
};

// Turn a perfect toc to a tree using <ul>
var tocToTree = function(toc) {
    var i;
    var front = '<li>';
    var ending = ['</li>'];
    var curLevel = 1;
    for (i in toc) {
        var item = toc[i];
        if (item.level == curLevel) {
            front += '</li>';
            front += '<li>';
            front += itemToHtml(item);
        } else if (item.level > curLevel) {
            // assert(item.level - curLevel == 1)
            front += '<ul>';
            ending.push('</ul>');
            front += '<li>';
            front += itemToHtml(item);
            ending.push('</li>');
            curLevel = item.level;
        } else {
            while (item.level < curLevel) {
                var ele = ending.pop();
                front += ele;
                if (ele == '</ul>') {
                    curLevel--;
                }
            }
            front += '</li>';
            front += '<li>';
            front += itemToHtml(item);
        }
    }
    while (ending.length > 0) {
        front += ending.pop();
    }
    front = front.replace("<li></li>", "");
    front = '<ul>' + front + '</ul>';
    return front;
};

var handleToc = function(needToc) {
    var tocTree = tocToTree(toPerfectToc(toc));
    content.setToc(tocTree);

    // Add it to html
    if (needToc) {
        var eles = document.getElementsByClassName('vnote-toc');
        for (var i = 0; i < eles.length; ++i) {
            eles[i].innerHTML = tocTree;
        }
    }
};

var mdHasTocSection = function(markdown) {
    var n = markdown.search(/(\n|^)\[toc\]/i);
    return n != -1;
};

var highlightCodeBlocks = function(doc, enableMermaid) {
    var codes = doc.getElementsByTagName('code');
    for (var i = 0; i < codes.length; ++i) {
        var code = codes[i];
        if (code.parentElement.tagName.toLowerCase() == 'pre') {
            if (enableMermaid && code.classList.contains('language-mermaid')) {
                // Mermaid code block.
                continue;
            } else {
                hljs.highlightBlock(code);
            }
        }
    }
};

var updateText = function(text) {
    var needToc = mdHasTocSection(text);
    var html = markdownToHtml(text, needToc);
    placeholder.innerHTML = html;
    handleToc(needToc);
    insertImageCaption();
    highlightCodeBlocks(document, VEnableMermaid);
    renderMermaid('language-mermaid');

    // If you add new logics after handling MathJax, please pay attention to
    // finishLoading logic.
    if (VEnableMathjax) {
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
    var html = renderer.makeHtml(text);

    var parser = new DOMParser();
    var htmlDoc = parser.parseFromString("<div id=\"showdown-container\">" + html + "</div>", 'text/html');
    highlightCodeBlocks(htmlDoc, false);

    html = htmlDoc.getElementById('showdown-container').innerHTML;

    delete parser;

    content.highlightTextCB(html, id, timeStamp);
}

