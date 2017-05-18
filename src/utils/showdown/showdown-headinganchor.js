(function (extension) {
    if (typeof showdown !== 'undefined') {
        // global (browser or nodejs global)
        extension(showdown);
    } else if (typeof define === 'function' && define.amd) {
        // AMD
        define(['showdown'], extension);
    } else if (typeof exports === 'object') {
        // Node, CommonJS-like
        module.exports = extension(require('showdown'));
    } else {
        // showdown was not found so we throw
        throw Error('Could not find showdown library');
    }
}(function (showdown) {
    // loading extension into shodown
    showdown.extension('headinganchor', function () {
        var headinganchor = {
            type: 'output',
            filter: function(source) {
                var nameCounter = 0;
                var parser = new DOMParser();
                var htmlDoc = parser.parseFromString("<div id=\"showdown-container\">" + source + "</div>", 'text/html');
                var eles = htmlDoc.querySelectorAll("h1, h2, h3, h4, h5, h6");

                for (var i = 0; i < eles.length; ++i) {
                    var ele = eles[i];
                    var level = parseInt(ele.tagName.substr(1));
                    var escapedText = 'toc_' + nameCounter++;

                    toc.push({
                        level: level,
                        anchor: escapedText,
                        title: ele.innerText
                    });

                    ele.setAttribute('id', escapedText);
                }

                var html = htmlDoc.getElementById('showdown-container').innerHTML;

                delete parser;

                return html;
            }
        };

        return [headinganchor];
    });
}));
