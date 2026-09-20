// A reversible view of the rendered reader DOM, not a second Markdown renderer.
(function() {
    'use strict';

    class Presentation {
        constructor() {
            this.requested = false;
            this.darkTheme = true;
            this.generation = 0;
            this.session = null;
            window.vxcore.on('fullMarkdownRendered', () => {
                // Let the renderer remove its load guard and dispatch any pending render first.
                Promise.resolve().then(() => this.start());
            });
            window.addEventListener('pagehide', () => this.setActive(false));
        }

        isActive() {
            return !!this.session && !this.session.restored;
        }

        report(p_active, p_error = '') {
            if (window.vxMarkdownAdapter) {
                window.vxMarkdownAdapter.setPresentationState(p_active, p_error);
            }
        }

        setActive(p_active, p_darkTheme) {
            if (!p_active) {
                const wasRequested = this.requested || this.isActive();
                this.requested = false;
                ++this.generation;
                if (this.session) {
                    this.restore(this.session);
                }
                if (wasRequested) {
                    this.report(false);
                }
                return;
            }
            this.darkTheme = p_darkTheme;
            if (!this.requested) {
                this.requested = true;
                ++this.generation;
            }
            this.start();
        }

        start() {
            const core = window.vxcore;
            if (!this.requested || this.session || !core.initialized
                || core.numOfOngoingWorkers > 0 || core.pendingData.text !== null) {
                return;
            }
            const session = {
                generation: this.generation,
                root: null,
                deck: null,
                ready: false,
                restored: false,
                moves: [],
                sections: [],
                codeControls: [],
                attributes: [],
                styles: [],
                scroll: [],
                x: window.scrollX,
                y: window.scrollY,
                focus: document.activeElement
            };
            this.session = session;
            try {
                if (typeof window.Reveal !== 'function') {
                    throw new Error('The bundled Reveal runtime is unavailable');
                }
                this.build(session, core.contentContainer);
                session.deck = new window.Reveal(session.root, {
                    embedded: true,
                    view: null,
                    scrollActivationWidth: null,
                    width: 1280,
                    height: 720,
                    margin: 0.04,
                    center: false,
                    display: 'flex',
                    hash: false,
                    history: false,
                    respondToHashChanges: false,
                    fragmentInURL: false,
                    postMessage: false,
                    postMessageEvents: false,
                    keyboard: false,
                    overview: false,
                    help: false,
                    jumpToSlide: false,
                    mouseWheel: false,
                    touch: false,
                    fragments: false,
                    autoAnimate: false,
                    autoSlide: false,
                    autoPlayMedia: false,
                    pause: false,
                    previewLinks: false,
                    hideInactiveCursor: false,
                    focusBodyOnPageVisibilityChange: false,
                    transition: 'none',
                    backgroundTransition: 'none'
                });
                Promise.resolve(session.deck.initialize()).then(() => {
                    session.ready = true;
                    if (!this.requested || session.generation !== this.generation
                        || session.restored) {
                        this.restore(session);
                        this.start();
                        return;
                    }
                    session.keydown = event => this.keydown(event);
                    document.addEventListener('keydown', session.keydown, true);
                    this.layout();
                    this.report(true);
                }).catch(error => this.fail(session, error));
            } catch (error) {
                // A synchronous initialize failure has no pending ready callback.
                session.ready = true;
                this.fail(session, error);
            }
        }

        fail(p_session, p_error) {
            const current = p_session.generation === this.generation && this.requested;
            p_session.ready = true;
            this.restore(p_session);
            if (current) {
                this.requested = false;
                this.report(false, String(p_error && p_error.message || p_error));
            } else {
                this.start();
            }
        }

        // Record only attributes this view changes. Null is distinct from an empty attribute.
        attribute(p_session, p_node, p_name, p_value) {
            p_session.attributes.push([p_node, p_name, p_node.getAttribute(p_name)]);
            if (p_value === null) {
                p_node.removeAttribute(p_name);
            } else {
                p_node.setAttribute(p_name, p_value);
            }
        }

        flow(p_parent, p_nodes) {
            for (const node of Array.from(p_parent.childNodes)) {
                const heading = node.firstElementChild;
                const content = heading && heading.nextElementSibling;
                // Only viewer-owned document-flow wrappers are transparent. A list, table,
                // blockquote, user section, etc. stays atomic even if it contains headings.
                if (node.nodeType === 1 && node.matches('section.vx-heading-fold')
                    && heading && typeof heading.__vxHeadingContent === 'string'
                    && content && content.matches('div.vx-heading-fold-content')) {
                    p_nodes.push(heading);
                    this.flow(content, p_nodes);
                } else {
                    p_nodes.push(node);
                }
            }
        }

        parentHeading(p_heading) {
            const copy = p_heading.cloneNode(true);
            for (const node of Array.from(copy.querySelectorAll(
                '.vx-heading-fold-toggle, .vx-header-anchor, button, input, select, textarea'))) {
                node.remove();
            }
            for (const node of [copy, ...Array.from(copy.querySelectorAll('*'))]) {
                for (const attr of Array.from(node.attributes)) {
                    if (attr.name === 'id' || (node.tagName === 'A' && attr.name === 'href')
                        || attr.name === 'tabindex'
                        || attr.name.startsWith('aria-') || attr.name.startsWith('on')) {
                        node.removeAttribute(attr.name);
                    }
                }
            }
            return copy;
        }

        build(p_session, p_content) {
            const flow = [];
            this.flow(p_content, flow);
            const meaningful = flow.filter(node => node.nodeType === 1
                || (node.nodeType === 3 && node.textContent.trim()));
            if (meaningful.length === 0) {
                throw new Error('There is no rendered content to present');
            }
            const title = meaningful[0].nodeName === 'H1'
                && meaningful.filter(node => node.nodeName === 'H1').length === 1;
            const root = document.createElement('div');
            root.id = 'vx-presentation';
            root.className = 'reveal';
            const slides = document.createElement('div');
            slides.className = 'slides';
            root.appendChild(slides);
            p_session.root = root;

            // Preserve scroll positions inside code blocks/tables as well as the reader viewport.
            for (const node of p_content.querySelectorAll('*')) {
                if (node.scrollTop || node.scrollLeft) {
                    p_session.scroll.push([node, node.scrollLeft, node.scrollTop]);
                }
            }
            // Rendered diagrams and transparent images were colored for the reader theme.
            // Preserve that surface, not the whole reader theme, on the black slide deck.
            const media = 'img, .vx-mermaid-graph, .vx-plantuml-graph, .vx-graphviz-graph, '
                + '.vx-flowchartjs-graph, .vx-wavedrom-graph';
            for (const surface of p_content.querySelectorAll(media)) {
                let background = '';
                for (let ancestor = surface; ancestor; ancestor = ancestor.parentElement) {
                    const color = window.getComputedStyle(ancestor).backgroundColor;
                    if (color !== 'transparent' && color !== 'rgba(0, 0, 0, 0)') {
                        background = color;
                        break;
                    }
                }
                if (!background) {
                    background = window.vxcore.originalBodyBackgroundColor || '';
                }
                const foreground = window.getComputedStyle(surface).color;
                this.attribute(p_session, surface, 'style', surface.getAttribute('style'));
                surface.style.backgroundColor = background;
                surface.style.color = foreground;
            }
            for (const pre of p_content.querySelectorAll('.code-toolbar > pre')) {
                // Collapse controls compute presentation-sized max-heights. Restore their
                // original state and icon nodes together, rather than leaving mismatched UI.
                const toolbar = pre.parentElement;
                const button = toolbar.querySelector('.vx-collapse-btn');
                this.attribute(p_session, pre, 'style', pre.getAttribute('style'));
                if (button) {
                    this.attribute(p_session, toolbar, 'class', toolbar.getAttribute('class'));
                    for (const name of ['aria-expanded', 'aria-label', 'title']) {
                        this.attribute(p_session, button, name, button.getAttribute(name));
                    }
                    p_session.codeControls.push([button, Array.from(button.childNodes)]);
                }
            }
            for (const content of p_content.querySelectorAll('.vx-heading-fold-content[hidden]')) {
                this.attribute(p_session, content, 'hidden', null);
            }
            for (const button of p_content.querySelectorAll('.vx-heading-fold-toggle')) {
                this.attribute(p_session, button, 'aria-expanded', 'true');
            }

            let body = null;
            let parent = null;
            for (const node of flow) {
                if (!body && node !== meaningful[0]) {
                    continue;
                }
                const heading = /^H[1-6]$/.test(node.nodeName);
                if (heading && node.nodeName === 'H1') {
                    parent = null;
                }
                const boundary = heading && (node.nodeName === 'H2' || node.nodeName === 'H3');
                if (!body || boundary) {
                    const slide = document.createElement('section');
                    slide.className = 'vx-presentation-slide';
                    if (!body && title) {
                        slide.classList.add('vx-presentation-title');
                    }
                    if (node.nodeName === 'H3' && parent) {
                        const header = document.createElement('div');
                        header.className = 'vx-presentation-parent';
                        header.appendChild(this.parentHeading(parent));
                        slide.appendChild(header);
                    }
                    body = document.createElement('div');
                    body.className = 'vx-presentation-body';
                    body.tabIndex = 0;
                    slide.appendChild(body);
                    slides.appendChild(slide);
                }
                if (heading && node.nodeName === 'H2') {
                    parent = node;
                }
                const placeholder = document.createComment('vx-presentation');
                node.parentNode.replaceChild(placeholder, node);
                p_session.moves.push([node, placeholder]);
                body.appendChild(node);
            }

            // Reveal reads attributes/classes on descendants too (lazy media, fragments,
            // fit-text, controls). Rendered note markup must not become deck configuration.
            const reserved = new Set(['reveal', 'slides', 'backgrounds', 'controls', 'progress',
                'slide-number', 'speaker-notes', 'notes', 'pause-overlay', 'aria-status', 'fragment',
                'r-fit-text', 'r-stretch', 'stretch', 'r-stack', 'enter-fullscreen',
                'navigate-left', 'navigate-right', 'navigate-up', 'navigate-down',
                'navigate-prev', 'navigate-next']);
            for (const element of slides.querySelectorAll('.vx-presentation-body *, .vx-presentation-parent *')) {
                for (const attr of Array.from(element.attributes)) {
                    if (attr.name.startsWith('data-') && attr.name !== 'data-source-line'
                        && attr.name !== 'data-source-line-end') {
                        this.attribute(p_session, element, attr.name, null);
                    }
                }
                if (Array.from(element.classList).some(name => reserved.has(name))) {
                    this.attribute(p_session, element, 'class', Array.from(element.classList)
                        .filter(name => !reserved.has(name)).join(' '));
                }
                if (element.matches('video, audio')) {
                    this.attribute(p_session, element, 'data-ignore', '');
                }
            }
            // Reveal treats EVERY descendant section as a slide, not just direct children.
            // Keep original section objects off-DOM; moving children preserves math/SVG IDs,
            // per-node listeners, worker caches and protected-link WeakMap entries.
            for (const section of Array.from(slides.querySelectorAll('section'))) {
                if (section.parentNode === slides) {
                    continue;
                }
                const replacement = document.createElement('div');
                for (const attr of section.attributes) {
                    replacement.setAttribute(attr.name, attr.value);
                }
                while (section.firstChild) {
                    replacement.appendChild(section.firstChild);
                }
                section.replaceWith(replacement);
                p_session.sections.push([section, replacement]);
            }
            if (typeof handleTaskListClick === 'function') {
                root.addEventListener('click', handleTaskListClick);
            }
            root.addEventListener('click', event => {
                const link = event.target.closest && event.target.closest('a[href^="#"]');
                // Protected links already handle trusted activation on the original node.
                if (link && !event.defaultPrevented && !window.vxOptions.protectedView) {
                    let anchor = link.getAttribute('href').substring(1);
                    try { anchor = decodeURIComponent(anchor); } catch (_) {}
                    if (this.scrollToAnchor(anchor)) {
                        event.preventDefault();
                    }
                }
            });
            this.attribute(p_session, p_content, 'hidden', '');
            for (const node of [document.documentElement, document.body]) {
                let classes = node.className + ' vx-presentation-active';
                if (node === document.documentElement && !this.darkTheme) {
                    classes += ' vx-presentation-light';
                }
                this.attribute(p_session, node, 'class', classes);
            }
            for (const link of document.querySelectorAll('[data-vx-presentation-style]')) {
                p_session.styles.push([link, link.getAttribute('media')]);
                link.media = 'all';
            }
            document.body.appendChild(root);
        }

        restore(p_session) {
            if (p_session.keydown) {
                document.removeEventListener('keydown', p_session.keydown, true);
                p_session.keydown = null;
            }
            // initialize() completes asynchronously. Do not destroy before its ready event:
            // Reveal would cancel start() and leave the initialization promise unresolved.
            if (p_session.ready && p_session.deck) {
                const rootClass = document.documentElement.getAttribute('class');
                try {
                    p_session.deck.destroy();
                } catch (error) {
                    console.error('Unable to destroy presentation', error);
                }
                p_session.deck = null;
                // A canceled initialization settles after the reader was already restored.
                if (p_session.restored) {
                    if (rootClass === null) {
                        document.documentElement.removeAttribute('class');
                    } else {
                        document.documentElement.setAttribute('class', rootClass);
                    }
                }
            }
            if (!p_session.restored) {
                p_session.restored = true;
                for (let i = p_session.sections.length - 1; i >= 0; --i) {
                    const [section, replacement] = p_session.sections[i];
                    while (replacement.firstChild) {
                        section.appendChild(replacement.firstChild);
                    }
                    replacement.replaceWith(section);
                }
                for (const [node, placeholder] of p_session.moves) {
                    placeholder.replaceWith(node);
                }
                for (const [button, children] of p_session.codeControls) {
                    if (button.childNodes.length !== children.length
                        || children.some((node, index) => button.childNodes[index] !== node)) {
                        while (button.firstChild) {
                            button.removeChild(button.firstChild);
                        }
                        for (const node of children) {
                            button.appendChild(node);
                        }
                    }
                }
                for (let i = p_session.attributes.length - 1; i >= 0; --i) {
                    const [node, name, value] = p_session.attributes[i];
                    if (value === null) {
                        node.removeAttribute(name);
                    } else {
                        node.setAttribute(name, value);
                    }
                }
                for (const [link, media] of p_session.styles) {
                    if (media === null) {
                        link.removeAttribute('media');
                    } else {
                        link.setAttribute('media', media);
                    }
                }
                if (p_session.root) {
                    p_session.root.remove();
                }
                for (const [node, x, y] of p_session.scroll) {
                    node.scrollLeft = x;
                    node.scrollTop = y;
                }
                if (p_session.focus && p_session.focus.isConnected) {
                    p_session.focus.focus({preventScroll: true});
                }
                window.scrollTo(p_session.x, p_session.y);
            }
            if (p_session.ready && this.session === p_session) {
                this.session = null;
            }
        }

        layout() {
            if (this.isActive() && this.session.ready) {
                this.session.deck.layout();
            }
        }

        scrollToAnchor(p_anchor) {
            if (!this.isActive() || !this.session.ready) {
                return false;
            }
            const target = document.getElementById(p_anchor);
            if (!target || !this.session.root.contains(target)) {
                return false;
            }
            const slide = target.closest('.vx-presentation-slide');
            const slides = this.session.deck.getSlides();
            this.session.deck.slide(slides.indexOf(slide));
            target.scrollIntoView({block: 'nearest'});
            return true;
        }

        keydown(p_event) {
            if (p_event.defaultPrevented || p_event.ctrlKey || p_event.metaKey || p_event.altKey
                || (window.vxImageViewer && window.vxImageViewer.isViewingImage())) {
                return;
            }
            const target = p_event.target;
            if (target && (target.isContentEditable
                || (target.closest && target.closest('input, textarea, select, button, a[href]')))) {
                return;
            }
            const deck = this.session.deck;
            const body = deck.getCurrentSlide().querySelector('.vx-presentation-body');
            switch (p_event.key) {
            case 'ArrowLeft': deck.prev(); break;
            case 'ArrowRight': deck.next(); break;
            case ' ': p_event.shiftKey ? deck.prev() : deck.next(); break;
            case 'ArrowUp': body.scrollTop -= 60; break;
            case 'ArrowDown': body.scrollTop += 60; break;
            case 'PageUp': body.scrollTop -= body.clientHeight * 0.9; break;
            case 'PageDown': body.scrollTop += body.clientHeight * 0.9; break;
            case 'Home': body.scrollTop = 0; break;
            case 'End': body.scrollTop = body.scrollHeight; break;
            default: return; // Escape remains native; never enable Reveal's F shortcut.
            }
            p_event.preventDefault();
            p_event.stopImmediatePropagation();
        }
    }

    window.vxPresentation = new Presentation();
})();
