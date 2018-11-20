var toc = [];

var setVisible = function(node, visible) {
    var cl = 'hide-none';
    if (visible) {
        node.classList.remove(cl);
    } else {
        node.classList.add(cl);
    }
};

var isVisible = function(node) {
    var cl = 'hide-none';
    return !node.classList.contains(cl);
};

var setPostContentExpanded = function(node, expanded) {
    var cl = 'col-expand';
    if (expanded) {
        node.classList.add(cl);
    } else {
        node.classList.remove(cl);
    }
};

var setOutlinePanelVisible = function(visible) {
    var outlinePanel = document.getElementById('outline-panel');
    var postContent = document.getElementById('post-content');

    setVisible(outlinePanel, visible);
    setPostContentExpanded(postContent, !visible);
};

var isOutlinePanelVisible = function() {
    var outlinePanel = document.getElementById('outline-panel');
    return isVisible(outlinePanel);
};

window.addEventListener('load', function() {
    var outlinePanel = document.getElementById('outline-panel');
    outlinePanel.style.display = 'initial';

    var floatingContainer = document.getElementById('container-floating');
    floatingContainer.style.display = 'initial';

    var outlineContent = document.getElementById('outline-content');
    var postContent = document.getElementById('post-content');

    // Escape @text to Html.
    var escapeHtml = function(text) {
        var map = {
            '&': '&amp;',
            '<': '&lt;',
            '>': '&gt;',
            '"': '&quot;',
            "'": '&#039;'
        };

        return text.replace(/[&<>"']/g, function(m) { return map[m]; });
    }

    // Fetch the outline.
    var headers = postContent.querySelectorAll("h1, h2, h3, h4, h5, h6");
    toc = [];
    for (var i = 0; i < headers.length; ++i) {
        var header = headers[i];

        toc.push({
            level: parseInt(header.tagName.substr(1)),
            anchor: header.id,
            title: escapeHtml(header.textContent)
        });
    }

    if (toc.length == 0) {
        setOutlinePanelVisible(false);
        setVisible(floatingContainer, false);
        return;
    }

    var baseLevel = baseLevelOfToc(toc);
    var tocTree = tocToTree(toPerfectToc(toc, baseLevel), baseLevel);

    outlineContent.innerHTML = tocTree;
    setOutlinePanelVisible(true);
    setVisible(floatingContainer, true);
});

// Return the topest level of @toc, starting from 1.
var baseLevelOfToc = function(p_toc) {
    var level = -1;
    for (i in p_toc) {
        if (level == -1) {
            level = p_toc[i].level;
        } else if (level > p_toc[i].level) {
            level = p_toc[i].level;
        }
    }

    if (level == -1) {
        level = 1;
    }

    return level;
};

// Handle wrong title levels, such as '#' followed by '###'
var toPerfectToc = function(p_toc, p_baseLevel) {
    var i;
    var curLevel = p_baseLevel - 1;
    var perfToc = [];
    for (i in p_toc) {
        var item = p_toc[i];

        // Insert empty header.
        while (item.level > curLevel + 1) {
            curLevel += 1;
            var tmp = { level: curLevel,
                        anchor: '',
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
    return '<a href="#' + item.anchor + '" data="' + item.anchor + '">' + item.title + '</a>';
};

// Turn a perfect toc to a tree using <ul>
var tocToTree = function(p_toc, p_baseLevel) {
    var i;
    var front = '<li>';
    var ending = ['</li>'];
    var curLevel = p_baseLevel;
    for (i in p_toc) {
        var item = p_toc[i];
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

var toggleMore = function() {
    if (toc.length == 0) {
        return;
    }

    var p = document.getElementById('floating-more');
    if (isOutlinePanelVisible()) {
        p.textContent = '<';
        setOutlinePanelVisible(false);
    } else {
        p.textContent = '>';
        setOutlinePanelVisible(true);
    }
};

window.addEventListener('scroll', function() {
    if (toc.length == 0 || !isOutlinePanelVisible()) {
        return;
    }

    var postContent = document.getElementById('post-content');
    var scrollTop = document.documentElement.scrollTop
                    || document.body.scrollTop
                    || window.pageYOffset;
    var eles = postContent.querySelectorAll("h1, h2, h3, h4, h5, h6");

    if (eles.length == 0) {
        return;
    }

    var idx = -1;
    var biaScrollTop = scrollTop + 50;
    for (var i = 0; i < eles.length; ++i) {
        if (biaScrollTop >= eles[i].offsetTop) {
            idx = i;
        } else {
            break;
        }
    }

    var header = '';
    if (idx != -1) {
        header = eles[idx].id;
    }

    highlightItemOnlyInOutline(header);
});

var highlightItemOnlyInOutline = function(id) {
    var cl = 'outline-bold';
    var outlineContent = document.getElementById('outline-content');
    var eles = outlineContent.querySelectorAll("a");
    var target = null;
    for (var i = 0; i < eles.length; ++i) {
        var ele = eles[i];
        if (ele.getAttribute('data') == id) {
            target = ele;
            ele.classList.add(cl);
        } else {
            ele.classList.remove(cl);
        }
    }

    // TODO: scroll target into view within the outline panel scroll area.
};
