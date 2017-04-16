var placeholder = document.getElementById('placeholder');
var renderer = new marked.Renderer();
var toc = []; // Table of contents as a list
var nameCounter = 0;

renderer.heading = function(text, level) {
    // Use number to avoid issues with Chinese
    var escapedText = 'toc_' + nameCounter++;
    toc.push({
        level: level,
        anchor: escapedText,
        title: text
    });
    return '<h' + level + ' id="' + escapedText + '">' + text + '</h' + level + '>';
};

// Highlight.js to highlight code block
marked.setOptions({
    highlight: function(code) {
        return hljs.highlightAuto(code).value;
    }
});

var markdownToHtml = function(markdown, needToc) {
    toc = [];
    nameCounter = 0;
    var html = marked(markdown, { renderer: renderer });
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

var updateText = function(text) {
    var needToc = mdHasTocSection(text);
    var html = markdownToHtml(text, needToc);
    placeholder.innerHTML = html;
    handleToc(needToc);
    renderMermaid('lang-mermaid');
    if (VEnableMathjax) {
        try {
            MathJax.Hub.Queue(["Typeset", MathJax.Hub, placeholder]);
        } catch (err) {
            content.setLog("err: " + err);
        }
    }
};

var highlightText = function(text, id, timeStamp) {
    var html = marked(text);
    content.highlightTextCB(html, id, timeStamp);
}

