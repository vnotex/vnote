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
                                       blankReplacement: (content, node) => {
                                           if (node.nodeName === 'PRE') {
                                               return this.codeBlockMarkdown(node);
                                           }

                                           if (node.nodeName == 'SPAN') {
                                               return content;
                                           }

                                           return node.isBlock ? '\n\n' : ''
                                       }});
        // Code blocks use the shared classifier below, not GFM's narrower DIV rule.
        this.ts.use([turndownPluginGfm.strikethrough,
                     turndownPluginGfm.tables,
                     turndownPluginGfm.taskListItems]);
        this.fixCodeBlock();
        this.fixMath();

        // TODO: verify and copy several rules from VNote 2.0.
        // No <head> and <style> parse.
        this.ts.remove(['head', 'style']);

        this.fixMark();

        this.fixHeading();

        this.fixParagraph();

        // MUST be called: without it the bundled default image rule wins and
        // every declared size is dropped.
        this.fixImage();
    }

    turndown(p_html) {
        // Normalize before Turndown collapses whitespace in non-PRE elements.
        // This detached document never installs clipboard markup in the live page.
        const root = document.implementation.createHTMLDocument('').body;
        root.innerHTML = p_html;
        this.normalizeCodeBlocks(root);
        this.normalizeMath(root);
        return this.ts.turndown(root);
    }

    normalizeMath(p_root) {
        // Accept only the formula shell, never a caption or surrounding prose.
        const isShell = (node, math) => {
            if (node === math) {
                return true;
            }
            if (node.nodeType === 3) {
                return !node.data.trim();
            }
            if (node.nodeType !== 1) {
                return true;
            }
            if (node.matches('.katex-html')) {
                return true;
            }
            return node.matches('.katex, .katex-display, .katex-mathml') &&
                Array.from(node.childNodes).every(child => isShell(child, math));
        };
        for (const math of p_root.querySelectorAll('math')) {
            if (!p_root.contains(math) || math.closest('pre, code')) {
                continue;
            }
            const annotation = Array.from(math.querySelectorAll('semantics > annotation')).find(node =>
                node.closest('math') === math &&
                (node.getAttribute('encoding') || '').trim().toLowerCase() === 'application/x-tex');
            const tex = (annotation ? annotation.textContent.trim() : '') || this.mathmlToTex(math);
            if (!tex) {
                continue;
            }
            const display = (math.getAttribute('display') || '').trim().toLowerCase() === 'block' ||
                !!math.closest('.katex-display') || /[\r\n]/.test(tex);
            let target = math;
            while (target.parentElement !== p_root &&
                   target.parentElement.querySelectorAll('math').length === 1 &&
                   isShell(target.parentElement, math)) {
                target = target.parentElement;
            }
            // Attributes survive Turndown's clone/whitespace collapse. A nonblank
            // placeholder keeps its blank rule from bypassing the math rule.
            const replacement = math.ownerDocument.createElement(display ? 'div' : 'span');
            replacement.setAttribute('data-vnote-math', tex);
            replacement.textContent = 'math';
            target.replaceWith(replacement);
        }
    }

    mathmlToTex(p_math) {
        // Browser selection copying can omit hidden TeX annotations. Recover only
        // understood presentation nodes; never turn a partial formula into TeX.
        const operators = new Map([
            ['−', '-'], ['⋅', '\\cdot '], ['×', '\\times '], ['÷', '\\div '],
            ['⋯', '\\cdots '], ['…', '\\ldots '], ['±', '\\pm '],
            ['≤', '\\le '], ['≥', '\\ge '], ['≠', '\\ne '], ['∞', '\\infty '],
            ['→', '\\to '], ['{', '\\{'], ['}', '\\}']
        ]);
        // MathML token roles do not restrict their contents to letters: KaTeX puts
        // bars, slashes and infinity in mi. Preserve Unicode math symbols without
        // growing a per-symbol TeX table, but never interpret TeX syntax as a glyph.
        const symbol = value => operators.get(value) ??
            (value !== '~' && /^(?:[+\-=<>(),.;:!/\[\]|]|\p{Sm})$/u.test(value) ? value : null);
        const fonts = new Map([
            ['italic', 'mathit'], ['bold', 'mathbf'], ['bold-italic', 'boldsymbol'],
            ['script', 'mathcal'], ['fraktur', 'mathfrak'], ['double-struck', 'mathbb'],
            ['sans-serif', 'mathsf'], ['monospace', 'mathtt']
        ]);
        // Keep named operators as operators (not upright identifiers), including
        // limit placement. Terminate control words before adjoining identifiers.
        const functions = new Set([
            'arcsin', 'arccos', 'arctan', 'arctg', 'arcctg', 'arg', 'ch', 'cos', 'cosec',
            'cosh', 'cot', 'cotg', 'coth', 'csc', 'ctg', 'cth', 'deg', 'dim', 'exp', 'hom',
            'ker', 'lg', 'ln', 'log', 'sec', 'sin', 'sinh', 'sh', 'tan', 'tanh', 'tg', 'th',
            'det', 'gcd', 'inf', 'lim', 'max', 'min', 'Pr', 'sup'
        ]);
        const normalVariant = node => !node.hasAttribute('mathvariant') ||
            node.getAttribute('mathvariant') === 'normal';
        const identifier = node => node?.localName === 'mi' && !node.children.length &&
            normalVariant(node) && /^\p{L}+$/u.test(node.textContent.trim());
        const application = node => node?.localName === 'mo' && !node.children.length &&
            normalVariant(node) && node.textContent.trim() === '\u2061';
        const brace = node => {
            if (!node || !['munder', 'mover'].includes(node.localName) || node.children.length !== 2) {
                return false;
            }
            const marker = node.children[1];
            return marker.localName === 'mo' && !marker.children.length &&
                marker.textContent.trim() === (node.localName === 'munder' ? '⏟' : '⏞');
        };
        const convert = node => {
            if (node.namespaceURI !== 'http://www.w3.org/1998/Math/MathML') {
                return null;
            }
            const name = node.localName;
            const children = Array.from(node.children);
            if (['mi', 'mn', 'mo', 'mtext'].includes(name)) {
                if (children.length || (name !== 'mi' && !normalVariant(node))) {
                    return null;
                }
                const value = node.textContent.trim();
                if (name === 'mi') {
                    if (!/^\p{L}+$/u.test(value)) {
                        const tex = symbol(value);
                        // An identifier bar is ordinary, not the relation atom of raw ∣.
                        return normalVariant(node) && tex !== null ? '\\mathord{' + tex.trim() + '}' : null;
                    }
                    if (application(node.nextElementSibling)) {
                        if (!normalVariant(node)) {
                            return null;
                        }
                        return functions.has(value) ? '\\' + value + ' ' : '\\operatorname{' + value + '}';
                    }
                    if (!normalVariant(node)) {
                        const font = fonts.get(node.getAttribute('mathvariant'));
                        // Bound styled alphabets to bundled glyphs; script/blackboard
                        // lowercase can silently fall back to an unstyled letter.
                        if (!font || !/^[A-Za-z]+$/.test(value) ||
                            (['mathcal', 'mathbb'].includes(font) && !/^[A-Z]+$/.test(value))) {
                            return null;
                        }
                        return '\\' + font + '{' + value + '}';
                    }
                    // Multi-letter identifiers default to upright in MathML.
                    return Array.from(value).length === 1 && !node.hasAttribute('mathvariant') ?
                        value : '\\mathrm{' + value + '}';
                }
                if (name === 'mn') {
                    return /^(?:\d+(?:\.\d*)?|\.\d+)$/.test(value) ? value : null;
                }
                if (name === 'mo') {
                    if (application(node)) {
                        return identifier(node.previousElementSibling) ? '' : null;
                    }
                    return symbol(value);
                }
                const text = node.textContent.replace(/\r\n|[\r\n\t\u00a0]/g, ' ')
                    .replace(/[\\{}$%&#_^~]/g, ch => {
                        if (ch === '\\') return '\\textbackslash{}';
                        if (ch === '^') return '\\textasciicircum{}';
                        if (ch === '~') return '\\textasciitilde{}';
                        return '\\' + ch;
                    });
                return '\\text{' + text + '}';
            }
            if (Array.from(node.childNodes).some(child => child.nodeType === 3 && child.data.trim())) {
                return null;
            }
            if (name === 'semantics') {
                const presentation = children.filter(child =>
                    !['annotation', 'annotation-xml'].includes(child.localName));
                return presentation.length === 1 ? convert(presentation[0]) : null;
            }
            if (brace(node)) {
                const base = convert(children[0]);
                return base === null ? null :
                    (name === 'munder' ? '\\underbrace{' : '\\overbrace{') + base + '}';
            }
            const parts = children.map(convert);
            if (parts.some(part => part === null)) {
                return null;
            }
            const baseName = children[0]?.localName;
            const functionBase = baseName === 'mrow' && children[0].children.length === 2 &&
                identifier(children[0].children[0]) && application(children[0].children[1]);
            const atomicBase = functionBase || ['mi', 'mo'].includes(baseName) ||
                (baseName === 'mn' && parts[0].length === 1);
            const scriptBase = atomicBase ? parts[0] : '{' + parts[0] + '}';
            switch (name) {
            case 'math':
            case 'mrow':
                return parts.join('');
            case 'mspace': {
                const width = node.getAttribute('width') || '';
                return !parts.length && /^(?:\d+(?:\.\d*)?|\.\d+)em$/.test(width) &&
                    !['height', 'depth', 'linebreak'].some(attr => node.hasAttribute(attr)) ?
                    '\\hspace{' + width + '}' : null;
            }
            case 'msqrt':
                return parts.length ? '\\sqrt{' + parts.join('') + '}' : null;
            case 'mfrac':
                return parts.length === 2 ? '\\frac{' + parts[0] + '}{' + parts[1] + '}' : null;
            case 'mroot':
                return parts.length === 2 ? '\\sqrt[{' + parts[1] + '}]{' + parts[0] + '}' : null;
            case 'msub':
            case 'msup':
                return parts.length === 2 ? scriptBase + (name === 'msub' ? '_{' : '^{') + parts[1] + '}' : null;
            case 'msubsup':
                return parts.length === 3 ? scriptBase + '_{' + parts[1] + '}^{' + parts[2] + '}' : null;
            case 'munderover':
                return parts.length === 3 ?
                    '\\overset{' + parts[2] + '}{\\underset{' + parts[1] + '}{' + parts[0] + '}}' : null;
            case 'munder':
            case 'mover':
                if (parts.length !== 2) {
                    return null;
                }
                if (children[0].localName === name && brace(children[0])) {
                    return parts[0] + (name === 'munder' ? '_{' : '^{') + parts[1] + '}';
                }
                return (name === 'munder' ? '\\underset{' : '\\overset{') + parts[1] + '}{' + parts[0] + '}';
            default:
                return null;
            }
        };
        // Keep internal TeX command separators, but not whitespace next to $ delimiters.
        return convert(p_math)?.trim() ?? null;
    }

    fixMath() {
        this.ts.addRule('math', {
            filter: node => node.hasAttribute('data-vnote-math'),
            replacement: (content, node) => {
                const tex = node.getAttribute('data-vnote-math');
                return node.nodeName === 'DIV' ? '\n\n$$\n' + tex + '\n$$\n\n' : '$' + tex + '$';
            }
        });
    }

    getCodeBlockLanguage(p_node) {
        if (!p_node) {
            return '';
        }
        const valid = value => /^[a-z0-9][a-z0-9_+#.-]*$/i.test(value);
        for (const name of ['data-language', 'data-lang']) {
            const value = (p_node.getAttribute(name) || '').trim();
            if (valid(value)) {
                return value.toLowerCase();
            }
        }
        for (const name of p_node.classList) {
            const match = /^(?:language-|lang-|highlight-(?:source|text)-)(.+)$/i.exec(name);
            if (match && valid(match[1])) {
                return match[1].toLowerCase();
            }
        }
        return '';
    }

    isCodeBlockChrome(p_node) {
        if (p_node.nodeType !== 1) {
            return false;
        }
        const name = p_node.nodeName.toLowerCase();
        const role = p_node.getAttribute('role');
        if (name === 'button' || name === 'svg' || role === 'button' || role === 'toolbar') {
            return true;
        }
        if (name === 'input' && /^(?:button|submit|reset)$/i.test(p_node.type)) {
            return true;
        }
        const classes = Array.from(p_node.classList);
        // Syntax tokens such as "token header" are source, not a banner.
        return !classes.some(value => value === 'token' || /^hljs-/.test(value)) &&
            classes.some(value => /(?:^|[-_])(?:banner|header|toolbar|gutter|line[-_]?numbers?)(?:$|[-_])/i.test(value));
    }

    isCodeBlockContent(p_node, p_body) {
        if (p_node === p_body || this.isCodeBlockChrome(p_node)) {
            return true;
        }
        if (p_node.nodeType === 3) {
            // Outside a dedicated code body, only layout whitespace is expendable.
            return !p_body || !p_node.data.trim();
        }
        if (p_node.nodeType !== 1) {
            return true;
        }
        if (!/^(?:DIV|SPAN|CODE|BR|B|I|EM|STRONG|MARK|SAMP|KBD|A|SUB|SUP)$/.test(p_node.nodeName)) {
            return false;
        }
        return Array.from(p_node.childNodes).every(node => this.isCodeBlockContent(node, p_body));
    }

    getCodeBlock(p_node) {
        let body = p_node;
        if (p_node.nodeName !== 'PRE') {
            if (p_node.nodeName !== 'DIV' || this.isCodeBlockChrome(p_node)) {
                return null;
            }
            const classes = Array.from(p_node.classList);
            // Bounded tokens, not arbitrary substrings such as "decode" or "code-description".
            const strongHint = classes.some(value =>
                /(?:^|[-_])(?:code[-_]?block|source[-_]?code)$/i.test(value) ||
                /^(?:hljs|prettyprint)$/.test(value));
            const weakHint = this.getCodeBlockLanguage(p_node) ||
                classes.some(value => /^(?:highlight|code)$/.test(value));
            if (!strongHint && !weakHint) {
                return null;
            }
            let bodies = p_node.querySelectorAll('pre');
            if (bodies.length === 0) {
                bodies = p_node.querySelectorAll('code');
            }
            if (bodies.length > 1) {
                return null;
            }
            body = bodies[0] || null;
            if (!body && !strongHint) {
                const tokens = Array.from(p_node.querySelectorAll('span[class]'));
                if (!tokens.some(node => Array.from(node.classList).some(value =>
                    value === 'token' || /^hljs-/.test(value)))) {
                    return null;
                }
            }
            if (!this.isCodeBlockContent(p_node, body)) {
                return null;
            }
            body = body || p_node;
        }
        let language = this.getCodeBlockLanguage(body.querySelector('code'));
        for (let node = body; !language && node; node = node === p_node ? null : node.parentElement) {
            language = this.getCodeBlockLanguage(node);
        }
        if (!language && p_node.nodeName === 'DIV') {
            // A marked banner may supply a language, but controls never supply its text.
            for (const node of p_node.querySelectorAll('[class], [role]')) {
                const isBanner = node.getAttribute('role') === 'toolbar' ||
                    Array.from(node.classList).some(value =>
                        /(?:^|[-_])(?:banner|header|toolbar)(?:$|[-_])/i.test(value));
                if (!isBanner || (body !== p_node && body.contains(node)) ||
                    !this.isCodeBlockChrome(node) || node.matches('button, input, svg, [role="button"]')) {
                    continue;
                }
                const banner = node.cloneNode(true);
                for (const control of banner.querySelectorAll('button, input, svg, [role="button"]')) {
                    control.remove();
                }
                const label = banner.textContent.trim();
                if (/^[a-z0-9][a-z0-9_+#.-]*$/i.test(label)) {
                    language = label.toLowerCase();
                    break;
                }
            }
        }
        return { body: body, language: language, stripChrome: body === p_node && p_node.nodeName === 'DIV' };
    }

    getCodeBlockText(p_node, p_stripChrome) {
        if (p_node.nodeType === 3) {
            return p_node.data;
        }
        if (p_node.nodeType !== 1 || (p_stripChrome && this.isCodeBlockChrome(p_node))) {
            return '';
        }
        if (p_node.nodeName === 'BR') {
            return '\n';
        }
        const children = Array.from(p_node.childNodes).filter(node =>
            !p_stripChrome || !this.isCodeBlockChrome(node));
        const hasLines = children.some(node => node.nodeName === 'DIV');
        let text = '';
        for (const node of children) {
            // Whitespace between explicit DIV lines is HTML layout, not another code line.
            if (hasLines && node.nodeType === 3 && !node.data.trim()) {
                continue;
            }
            const content = this.getCodeBlockText(node, p_stripChrome);
            if (node.nodeName === 'DIV' && text && !text.endsWith('\n')) {
                text += '\n';
            }
            text += content;
            if (node.nodeName === 'DIV' && (!content || !text.endsWith('\n'))) {
                text += '\n';
            }
        }
        return text;
    }

    normalizeCodeBlocks(p_root) {
        for (const node of p_root.querySelectorAll('pre, div')) {
            if (!p_root.contains(node)) {
                continue; // An accepted outer wrapper already consumed this subtree.
            }
            const block = this.getCodeBlock(node);
            if (!block) {
                continue;
            }
            const pre = node.ownerDocument.createElement('pre');
            pre.textContent = this.getCodeBlockText(block.body, block.stripChrome);
            pre.setAttribute('data-language', block.language);
            node.replaceWith(pre);
        }
    }

    codeBlockMarkdown(p_node) {
        const code = p_node.textContent;
        let fenceSize = 3;
        for (const match of code.matchAll(/`+/g)) {
            fenceSize = Math.max(fenceSize, match[0].length + 1);
        }
        const fence = '`'.repeat(fenceSize);
        return '\n\n' + fence + this.getCodeBlockLanguage(p_node) + '\n' +
            code.replace(/\n$/, '') + '\n' + fence + '\n\n';
    }

    fixCodeBlock() {
        this.ts.addRule('codeBlock', {
            filter: 'pre',
            replacement: (content, node) => this.codeBlockMarkdown(node)
        });
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

    fixHeading() {
        this.ts.addRule('heading', {
            filter: ['h1', 'h2', 'h3', 'h4', 'h5', 'h6'],
            replacement: function(content, node) {
                // The ATX prefix prevents list parsing. Literal backslashes are
                // doubled by Turndown, so undo only its single leading list escape.
                const heading = content.replace(/^(\d+)\\\. /, '$1. ');
                return '\n\n' + '#'.repeat(Number(node.nodeName[1])) + ' ' + heading + '\n\n';
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
