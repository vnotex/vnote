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
        // No <head> and <style> parse.
        this.ts.remove(['head', 'style']);

        this.fixMark();

        this.fixParagraph();

        // MUST be called: without it the bundled default image rule wins and
        // every declared size is dropped.
        this.fixImage();
    }

    turndown(p_html) {
        let markdown = this.ts.turndown(p_html);
        return markdown;
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

    fixParagraph() {
        this.ts.addRule('paragraph', {
            filter: 'p',
            replacement: function(content) {
                // Replace leading spaces with &nbsp; to avoid being parsed as code block.
                let lRe = /^\s+/;
                let ret = lRe.exec(content);
                if (ret) {
                    let leadingSpaces = ret[0];
                    if (leadingSpaces.length > 3) {
                        content = '&nbsp;'.repeat(leadingSpaces.length) + content.slice(leadingSpaces.length);
                    }
                }

                return '\n\n' + content + '\n\n'
            }
        });
    }

    fixImage() {
        // Preserve a declared size on HTML -> Markdown conversion.
        //
        // Markdown has no portable way to express one, so a sized image is
        // emitted as the canonical HTML tag instead -- the SAME spelling
        // vte::MarkdownUtils::generateImageTag() produces (self-closing,
        // double-quoted, attribute order src alt title width height), so both
        // generators round trip through the one C++ scanner. Without this rule
        // the bundled default image rule emits plain Markdown and "Parse to
        // Markdown and Paste" silently strips every size.
        let escapeAttr = function(value) {
            return String(value)
                .replace(/&/g, '&amp;')
                .replace(/</g, '&lt;')
                .replace(/>/g, '&gt;')
                .replace(/"/g, '&quot;')
                .replace(/'/g, '&#39;');
        };

        // The same "valid positive integer" rule as the C++ side: a percentage,
        // a non-integer or a non-positive value is no size at all.
        let positiveInt = function(value) {
            if (typeof value !== 'string' || !/^[0-9]+$/.test(value.trim())) {
                return 0;
            }
            let n = parseInt(value.trim(), 10);
            return n > 0 ? n : 0;
        };

        this.ts.addRule('img_fix', {
            filter: 'img',
            replacement: function (content, node) {
                let src = node.getAttribute('src') || '';
                if (!src) {
                    return '';
                }

                let alt = node.alt || '';
                let title = node.title || '';

                let width = positiveInt(node.getAttribute('width'));
                let height = positiveInt(node.getAttribute('height'));

                if (width > 0 || height > 0) {
                    let tag = '<img src="' + escapeAttr(src) + '"';
                    if (alt) {
                        tag += ' alt="' + escapeAttr(alt) + '"';
                    }
                    if (title) {
                        tag += ' title="' + escapeAttr(title) + '"';
                    }
                    if (width > 0) {
                        tag += ' width="' + width + '"';
                    }
                    if (height > 0) {
                        tag += ' height="' + height + '"';
                    }
                    return tag + ' />';
                }

                if (/[\r\n\[\]]/g.test(alt)) {
                    alt = '';
                }
                if (/[\r\n\)"]/g.test(title)) {
                    title = '';
                }

                let titlePart = title ? ' "' + title + '"' : '';
                return '![' + alt + ']' + '(' + src + titlePart + ')';
            }
        });
    }
}
