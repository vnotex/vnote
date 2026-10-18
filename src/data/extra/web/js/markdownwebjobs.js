// Markdown algorithms are providers of the generic job protocol, not document RPCs.
(() => {
    let previewer = null;
    let turndown = null;
    const jobs = window.vxWebJobs;
    const core = () => window.vxcore;
    const preview = () => {
        if (!previewer) {
            previewer = new GraphPreviewer(core(), core().inplacePreviewContainer);
        }
        return previewer;
    };

    jobs.registerHandler('markdown.previewGraph', (args, context) =>
        preview().previewGraph(args, context));
    jobs.registerHandler('markdown.previewMath', (args, context) =>
        preview().previewMath(args, context));

    jobs.registerHandler('markdown.highlightCode', (args) => {
        const match = /^```[^\S\n]*(\S+)?\s*\n([\s\S]+)\n```\s*$/.exec(args.source);
        if (!match || !match[1] || !match[2] || !Prism.languages[match[1]]) {
            return '';
        }
        return Prism.highlight(match[2], Prism.languages[match[1]], match[1]);
    });

    jobs.registerHandler('markdown.highlightMath', (args) => {
        if (!Prism.languages.latex) {
            return '';
        }
        // Each line must contain complete spans: the native highlighter consumes
        // individual lines, while Prism's whole-formula tokens may cross newlines.
        return args.source.split('\n').map(line =>
            Prism.highlight(line, Prism.languages.latex, 'latex')).join('\n');
    });

    jobs.registerHandler('markdown.parseStyleSheet', (args) => {
        const doc = document.implementation.createHTMLDocument('');
        const style = doc.createElement('style');
        style.textContent = args.source;
        doc.body.appendChild(style);
        const styles = [];
        for (const rule of style.sheet.cssRules) {
            if (rule.type === CSSRule.STYLE_RULE) {
                styles.push({
                    selector: rule.selectorText,
                    color: rule.style.color,
                    backgroundColor: rule.style.backgroundColor,
                    fontWeight: rule.style.fontWeight,
                    fontStyle: rule.style.fontStyle
                });
            }
        }
        return styles;
    });

    jobs.registerHandler('markdown.htmlToMarkdown', (args) => {
        if (!turndown) {
            turndown = new TurndownConverter(core());
        }
        return turndown.turndown(args.source);
    });

    jobs.registerHandler('markdown.headingAnchor', (args) => {
        const worker = core().getWorker('markdownit');
        if (!worker) {
            throw new Error('Markdown parser unavailable');
        }
        // Parse in the worker's isolated heading state, never render a document.
        return worker.getHeadingAnchor(args.source, args.lineNumber);
    });

    // Internal lifecycle operation, serialized after any cancelled render drains.
    jobs.registerHandler('markdown.releaseScope', (args) => {
        core().getWorker('math').releasePreviewScope(args.scope);
        return null;
    });

    window.vxMarkdownJobsRegistered = true;

    // Provider registration must finish before readiness can be announced. The
    // generic bridge may bind before or after window.load; it joins both barriers.
    const ready = () => jobs.setReady();
    core().on('ready', ready);
    if (core().initialized) {
        ready();
    }
})();
