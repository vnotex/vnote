// Convert HTML to Markdown.
class TurndownConverter {
    constructor(p_adapter) {
        this.adapter = p_adapter;

        turndownPluginGfm.options.autoHead = true;

        this.ts = new TurndownService({headingStyle: 'atx',
                                       bulletListMarker: '*',
                                       emDelimiter: '*',
                                       hr: '***',
                                       codeBlockStyle: 'fenced',
                                       blankReplacement: function(content, node) {
                                           if (node.nodeName == 'SPAN') {
                                               return content;
                                           }

                                           return node.isBlock ? '\n\n' : ''
                                       }});
        this.ts.use(turndownPluginGfm.gfm);

        // TODO: verify and copy several rules from VNote 2.0.
        this.fixMark();
    }

    turndown(p_html) {
        return this.ts.turndown(p_html);
    }

    // Trim a string into 3 parts: leading spaces, content, trailing spaces.
    trimString(p_str) {
        let result = { leadingSpaces: '',
                       content: '',
                       trailingSpaces: ''
                     };
        if (!p_str) {
            return result;
        }

        let lRe = /^\s+/;
        let ret = lRe.exec(p_str);
        if (ret) {
            result.leadingSpaces = ret[0];
            if (result.leadingSpaces.length == p_str.length) {
                return result;
            }
        }

        let tRe = /\s+$/;
        ret = tRe.exec(p_str);
        if (ret) {
            result.trailingSpaces = ret[0];
        }

        result.content = p_str.slice(result.leadingSpaces.length, p_str.length - result.trailingSpaces.length);
        return result;
    };

    fixMark() {
        this.ts.addRule('mark', {
            filter: 'mark',
            replacement: function(content, node, options) {
                if (!content) {
                    return '';
                }

                return '<mark>' + content + '</mark>';
            }
        });
    }

    fixImage() {
        this.ts.addRule('img_fix', {
            filter: 'img',
            replacement: function (content, node) {
                let alt = node.alt || '';
                if (/[\r\n\[\]]/g.test(alt)) {
                    alt= '';
                }

                let src = node.getAttribute('src') || '';

                let title = node.title || '';
                if (/[\r\n\)"]/g.test(title)) {
                    title = '';
                }

                let titlePart = title ? ' "' + title + '"' : '';
                return src ? '![' + alt + ']' + '(' + src + titlePart + ')' : ''
            }
        });
    }
}
