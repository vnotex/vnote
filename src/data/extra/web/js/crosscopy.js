// Class to cross copy contents to different targets.
class CrossCopy {
    constructor(p_adapter) {
        this.adapter = p_adapter;

        // Target name -> {rules}.
        this.targets = {};

        // Add targets here.
        this.addTargetNoBackground();

        this.addTargetEvernote();

        this.addTargetOneNote();

        this.addTargetMicrosoftWord();

        this.addTargetWeChatPublicAccountEditor();

        this.addTargetRawHtml();

        this.updateTargets();

        // Mark styles used to transform to span.
        this.markStyle = null;
    }

    updateTargets() {
        let targets = [];
        for (let key in this.targets) {
            targets.push(key);
        }

        this.adapter.setCrossCopyTargets(targets);
    }

    // ATTENTION: please add one entry in MarkdownViewerAdapter's getCrossCopyTargetDisplayName() for translations.
    addTarget(p_name, p_callback) {
        this.targets[p_name] = p_callback;
    }

    addTargetNoBackground() {
        let rules = [
            CrossCopy.rule_removeBackground.bind(undefined, ['mark', 'svg']),
            CrossCopy.rule_fixRelativeImage
        ];

        this.addTarget('No Background', {
            rules: rules
        });
    }

    addTargetEvernote() {
        let rules = [
            CrossCopy.rule_addFragment,
            CrossCopy.rule_removeBackground.bind(undefined, ['mark', 'pre', 'svg']),
            CrossCopy.rule_embedLocalImages,
            CrossCopy.rule_svgToImage
        ];

        this.addTarget('Evernote', {
            rules: rules
        });
    }

    addTargetOneNote() {
        let rules = [
            CrossCopy.rule_addFragment,
            CrossCopy.rule_removeBackground.bind(undefined, ['mark', 'svg']),
            CrossCopy.rule_replaceSvgWithLabel,
            CrossCopy.rule_fixRelativeImage,
            CrossCopy.rule_removeMarginPadding.bind(undefined, []),
            CrossCopy.rule_transformMarkToSpan
        ];

        this.addTarget('OneNote', {
            rules: rules
        });
    }

    addTargetMicrosoftWord() {
        let rules = [
            CrossCopy.rule_removeBackground.bind(undefined, ['mark', 'pre', 'svg']),
            CrossCopy.rule_replaceSvgWithLabel,
            CrossCopy.rule_fixRelativeImage,
            CrossCopy.rule_removeMarginPadding.bind(undefined, []),
            CrossCopy.rule_transformMarkToSpan,
        ];

        this.addTarget('Microsoft Word', {
            rules: rules
        });
    }

    addTargetWeChatPublicAccountEditor() {
        let rules = [
            CrossCopy.rule_removeBackground.bind(undefined, ['mark', 'pre', 'svg']),
            CrossCopy.rule_embedLocalImages,
            CrossCopy.rule_svgToImage,
            CrossCopy.rule_removeMarginPadding.bind(undefined, [])
        ];

        this.addTarget('WeChat Public Account Editor', {
            rules: rules
        });
    }

    addTargetRawHtml() {
        let rules = [
            CrossCopy.rule_removeAllStyles.bind(undefined, [])
        ];

        this.addTarget('Raw HTML', {
            rules: rules
        });
    }

    async crossCopy(p_id, p_timeStamp, p_target, p_baseUrl, p_html) {
        let target = this.targets[p_target];
        if (!target) {
            console.error("no matching cross-copy target", p_target);
            this.adapter.setCrossCopyResult(p_id, p_timeStamp, p_html);
            return;
        }

        let info = {
            inst: this,
            baseUrl: p_baseUrl
        };

        let result = await this.executeRules(target.rules, info, p_html);
        this.adapter.setCrossCopyResult(p_id, p_timeStamp, result);
    }

    async executeRules(p_rules, p_info, p_html) {
        if (!p_rules || p_rules.length == 0) {
            return p_html;
        }

        let doc = new DOMParser().parseFromString(p_html, "text/html");

        // Remove <head>.
        let htmlNode = doc.firstElementChild;
        htmlNode.removeChild(htmlNode.firstElementChild);

        // Remove the copy button in code blocks.
        let codeToolBars = doc.querySelectorAll('div.code-toolbar div.toolbar');
        for (let i = 0; i < codeToolBars.length; ++i) {
            let paNode = codeToolBars[i].parentNode;
            paNode.removeChild(codeToolBars[i]);
        }

        // Go through each rule.
        for (let i = 0; i < p_rules.length; ++i) {
            await p_rules[i](p_info, doc);
        }

        return doc.documentElement.outerHTML;
    }

    getMarkStyle() {
        if (this.markStyle) {
            return this.markStyle;
        }

        let marks = this.adapter.contentContainer.getElementsByTagName('mark');
        if (marks.length > 0) {
            let style = window.getComputedStyle(marks[0], null);
            this.markStyle = {
                color: style.color,
                backgroundColor: style.backgroundColor
            }
        }
        return this.markStyle;
    }

    // Add <!--StartFragment--> and <!--EndFragment--> inside <body>.
    static rule_addFragment(p_info, p_doc) {
        let bodyNode = p_doc.getElementsByTagName('body')[0];
        let startNode = p_doc.createComment('StartFragment');
        let endNode = p_doc.createComment('EndFragment');
        if (bodyNode.firstChild) {
            bodyNode.insertBefore(startNode, bodyNode.firstChild);
            bodyNode.appendChild(endNode);
        } else {
            bodyNode.appendChild(startNode);
            bodyNode.appendChild(endNode);
        }
    }

    // Remove background color of all tags except @p_tagsToExclude.
    static rule_removeBackground(p_tagsToExclude, p_info, p_doc) {
        CrossCopy.removeStyles(p_tagsToExclude, ['background', 'backgroundColor'], p_doc);
    }

    static rule_fixRelativeImage(p_info, p_doc) {
        let imgs = p_doc.getElementsByTagName('img');
        for (let i = 0; i < imgs.length; ++i) {
            let img = imgs[i];
            let src = img.getAttribute('src');
            let httpRegExp = new RegExp('^http[s]://');
            if (httpRegExp.test(src)) {
                continue;
            }

            let dataRegExp = new RegExp('^data:image/');
            if (dataRegExp.test(src)) {
                continue;
            }

            let fileRegExp = new RegExp('^file://');
            if (!fileRegExp.test(src)) {
                // img.src will automatically resolve the absolute url from relative one.
                img.setAttribute('src', img.src);
            }

            // Check if we need to fix the encoding.
            // Win needs only space-encoded.
            if (window.vxcore.os === "Windows") {
                let decodedUrl = decodeURI(img.src);
                if (decodedUrl.length != img.src.length) {
                    // May need other encoding.
                    img.src = decodedUrl.replace(/ /g, '%20');
                }
            }
        }
    }

    static rule_removeMarginPadding(p_tagsToExclude, p_info, p_doc) {
        CrossCopy.removeStyles(p_tagsToExclude,
                               ['margin-left', 'margin-right', 'padding-left', 'padding-right'],
                               p_doc);
    }

    static removeStyles(p_tagsToExclude, p_styles, p_doc) {
        let allElements = p_doc.getElementsByTagName('*');
        for (let i = 0; i < allElements.length; ++i) {
            let ele = allElements[i];
            if (p_tagsToExclude.indexOf(ele.tagName.toLowerCase()) > -1) {
                continue;
            }

            for (let j = 0; j < p_styles.length; ++j) {
                ele.style.removeProperty(p_styles[j]);
            }
        }
    }

    static rule_transformMarkToSpan(p_info, p_doc) {
        let marks = p_doc.getElementsByTagName('mark');
        while (marks.length > 0) {
            let mark = marks[0];
            let spanNode = p_doc.createElement('span');
            spanNode.innerHTML = mark.innerHTML;

            let markStyle = p_info.inst.getMarkStyle();
            if (markStyle) {
                spanNode.style.color = markStyle.color;
                spanNode.style.backgroundColor = markStyle.backgroundColor;
            }

            mark.parentNode.replaceChild(spanNode, mark);
        }
    }

    // Seems not needed with Prism highlight.
    static rule_useCodeBackgroundForPre(p_info, p_doc) {
        let preCodes = p_doc.querySelectorAll('pre code');
        for (let i = 0; i < preCodes.length; ++i) {
            let preNode = preCodes[i].parentNode;
            preNode.style.background = preCodes[i].style.background;
            preNode.style.backgroundColor = preCodes[i].style.backgroundColor;
        }
    }

    static localImageUrl(p_src, p_baseUrl) {
        const src = (p_src || '').trim();
        // A protocol-relative web URL must not become a file://host URL.
        if (!src || src.startsWith('//')) {
            return null;
        }
        const url = new URL(src, p_baseUrl || document.baseURI);
        return url.protocol === 'file:' || url.protocol === 'blob:' ? url : null;
    }

    static readImageDataUrl(p_url) {
        return new Promise((resolve, reject) => {
            Utils.httpGet(p_url.href, 'blob', (blob) => {
                if (!blob || !blob.size) {
                    reject(new Error('Unable to read local image'));
                    return;
                }
                // Some file handlers omit the MIME type.
                if (!blob.type || blob.type === 'application/octet-stream') {
                    const types = {png: 'png', jpg: 'jpeg', jpeg: 'jpeg', gif: 'gif',
                                   svg: 'svg+xml', webp: 'webp', bmp: 'bmp', avif: 'avif',
                                   ico: 'x-icon'};
                    const type = types[p_url.pathname.split('.').pop().toLowerCase()];
                    if (type) {
                        blob = blob.slice(0, blob.size, 'image/' + type);
                    }
                }
                const reader = new FileReader();
                reader.onload = () => resolve(reader.result);
                reader.onerror = reader.onabort = () => reject(new Error('Unable to encode image'));
                reader.readAsDataURL(blob);
            }, 30000);
        });
    }

    static async rule_embedLocalImages(p_info, p_doc) {
        await Promise.all(Array.from(p_doc.getElementsByTagName('img')).map(async (img) => {
            try {
                const url = CrossCopy.localImageUrl(img.getAttribute('src'), p_info.baseUrl);
                if (url) {
                    img.setAttribute('src', await CrossCopy.readImageDataUrl(url));
                    img.removeAttribute('srcset');
                }
            } catch (error) {
                // Keep unreadable images, rather than deleting them or blocking the copy.
                console.warn('failed to embed cross-copy image', error);
            }
        }));
    }

    // Generic rule: rasterize outer SVGs and SVG image sources, independent of the target.
    static async rule_svgToImage(p_info, p_doc) {
        const svgs = Array.from(p_doc.querySelectorAll('svg')).filter((svg) =>
            !svg.parentElement.closest('svg'));
        for (const svg of svgs) {
            try {
                const raster = await CrossCopy.rasterizeSvg(svg, p_info, true);
                const img = p_doc.createElement('img');
                img.src = raster.dataUrl;
                img.style.cssText = svg.style.cssText;
                img.style.width = raster.width + 'px';
                img.style.height = raster.height + 'px';
                img.width = Math.max(1, Math.round(raster.width));
                img.height = Math.max(1, Math.round(raster.height));
                img.alt = svg.getAttribute('aria-label') || (svg.querySelector('title') || {}).textContent || '';
                if (svg.hasAttribute('title')) {
                    img.title = svg.getAttribute('title');
                }
                const parent = svg.parentNode;
                parent.replaceChild(img, svg);
                if (parent.tagName.toLowerCase() === 'mjx-container') {
                    const assistive = parent.querySelector('mjx-assistive-mml');
                    if (assistive) {
                        assistive.remove();
                    }
                }
            } catch (error) {
                console.warn('failed to rasterize cross-copy SVG', error);
            }
        }

        for (const img of Array.from(p_doc.getElementsByTagName('img'))) {
            try {
                let src = (img.getAttribute('src') || '').trim();
                const url = CrossCopy.localImageUrl(src, p_info.baseUrl);
                if (url && (url.protocol === 'blob:' || /\.svg$/i.test(url.pathname))) {
                    src = await CrossCopy.readImageDataUrl(url);
                }
                if (!/^data:image\/svg\+xml[;,]/i.test(src)) {
                    continue;
                }
                const text = await new Promise((resolve, reject) => {
                    Utils.httpGet(src, 'text', (response) => response ? resolve(response)
                        : reject(new Error('Unable to read SVG image')), 30000);
                });
                const doc = new DOMParser().parseFromString(text, 'image/svg+xml');
                if (doc.querySelector('parsererror') || doc.documentElement.localName !== 'svg') {
                    throw new Error('Invalid SVG image');
                }
                const raster = await CrossCopy.rasterizeSvg(doc.documentElement, p_info);
                if (!img.hasAttribute('width') && !img.hasAttribute('height')
                    && !img.style.width && !img.style.height) {
                    img.width = Math.max(1, Math.round(raster.width));
                    img.height = Math.max(1, Math.round(raster.height));
                }
                img.src = raster.dataUrl;
                img.removeAttribute('srcset');
            } catch (error) {
                console.warn('failed to rasterize cross-copy SVG image', error);
            }
        }
    }

    static async rasterizeSvg(p_svg, p_info, p_matchLiveSvg = false) {
        // IDs inside an SVG image file belong to that file, not the viewer document.
        const container = p_matchLiveSvg && p_info.inst.adapter.contentContainer;
        let live = null;
        if (container) {
            // MathJax roots may have no ID, but their local glyph definitions do.
            const ids = [p_svg].concat(Array.from(p_svg.querySelectorAll('[id]')));
            for (const node of ids) {
                if (!node.id) {
                    continue;
                }
                const matches = container.querySelectorAll('[id="' + CSS.escape(node.id) + '"]');
                if (matches.length === 1) {
                    live = matches[0].closest('svg');
                    while (live && live.parentElement.closest('svg')
                           && container.contains(live.parentElement.closest('svg'))) {
                        live = live.parentElement.closest('svg');
                    }
                    if (live) {
                        break;
                    }
                }
            }
        }
        const source = live || p_svg;
        const clone = source.cloneNode(true);
        if (live) {
            const originals = [live].concat(Array.from(live.querySelectorAll('*')));
            const copies = [clone].concat(Array.from(clone.querySelectorAll('*')));
            originals.forEach((node, index) => {
                // Definition paint inherits from each <use>, not its hidden <defs> tree.
                if (node.closest('defs, symbol')) {
                    return;
                }
                const style = window.getComputedStyle(node);
                for (const property of style) {
                    copies[index].style.setProperty(property, style.getPropertyValue(property));
                }
            });
        } else {
            // Clipboard fragments carry inherited text styles on wrapping spans.
            for (const property of ['color', 'font-size', 'font-family', 'font-weight']) {
                for (let node = p_svg; node; node = node.parentElement) {
                    const value = node.style && node.style.getPropertyValue(property);
                    if (value) {
                        clone.style.setProperty(property, value);
                        break;
                    }
                }
            }
        }
        for (const image of clone.querySelectorAll('image')) {
            const href = image.getAttribute('href') || image.getAttributeNS('http://www.w3.org/1999/xlink', 'href');
            const url = CrossCopy.localImageUrl(href, p_info.baseUrl);
            if (url) {
                const dataUrl = await CrossCopy.readImageDataUrl(url);
                image.setAttribute('href', dataUrl);
                image.setAttributeNS('http://www.w3.org/1999/xlink', 'xlink:href', dataUrl);
            }
        }
        const load = () => new Promise((resolve, reject) => {
            const uri = 'data:image/svg+xml;charset=utf-8,'
                + encodeURIComponent(new XMLSerializer().serializeToString(clone));
            SvgToImage.loadImage(uri, {}, (error, image) => error ? reject(error) : resolve(image));
        });
        const box = live && live.getBoundingClientRect();
        let width = box && box.width;
        let height = box && box.height;
        if (!(width > 0 && height > 0)) {
            const viewBox = clone.viewBox.baseVal;
            if (viewBox.width > 0 && viewBox.height > 0) {
                if (!clone.hasAttribute('width') || clone.getAttribute('width').includes('%')) {
                    clone.setAttribute('width', parseFloat(clone.style.maxWidth) || viewBox.width);
                }
                if (!clone.hasAttribute('height') || clone.getAttribute('height').includes('%')) {
                    clone.setAttribute('height', viewBox.height);
                }
            }
            const image = await load();
            width = image.naturalWidth;
            height = image.naturalHeight;
        }
        if (!(width > 0 && height > 0)) {
            throw new Error('Empty SVG bounds');
        }
        // Scale the vector viewport, not an already-decoded low-resolution bitmap.
        const scale = Math.min(Math.max(2, window.devicePixelRatio || 1),
                               8192 / Math.max(width, height),
                               Math.sqrt(40000000 / (width * height)));
        const canvas = document.createElement('canvas');
        canvas.width = Math.max(1, Math.floor(width * scale));
        canvas.height = Math.max(1, Math.floor(height * scale));
        if (!clone.hasAttribute('viewBox')) {
            clone.setAttribute('viewBox', '0 0 ' + width + ' ' + height);
        }
        clone.setAttribute('width', canvas.width);
        clone.setAttribute('height', canvas.height);
        clone.style.width = canvas.width + 'px';
        clone.style.height = canvas.height + 'px';
        clone.style.maxWidth = 'none';
        clone.style.maxHeight = 'none';
        const image = await load();
        canvas.getContext('2d').drawImage(image, 0, 0);
        const dataUrl = canvas.toDataURL('image/png');
        if (!dataUrl.startsWith('data:image/png;base64,')) {
            throw new Error('Unable to encode SVG raster');
        }
        return {dataUrl: dataUrl, width: width, height: height};
    }

    static rule_removeAllStyles(p_tagsToExclude, p_info, p_doc) {
        let allElements = p_doc.getElementsByTagName('*');
        for (let i = 0; i < allElements.length; ++i) {
            let ele = allElements[i];
            if (p_tagsToExclude.indexOf(ele.tagName.toLowerCase()) > -1) {
                continue;
            }

            ele.style = '';
        }
    }

    static rule_replaceSvgWithLabel(p_info, p_doc) {
        let allSvgs = p_doc.getElementsByTagName('svg');
        while (allSvgs.length > 0) {
            let spanNode = p_doc.createElement('span');
            spanNode.style = 'font-weight: bold; color: white; background-color: red;'
            spanNode.textContent = 'INSERT_SVG_HERE';

            let node = allSvgs[0];
            if (node.parentNode.childElementCount == 1 && node.parentNode.tagName.toLowerCase() === 'div') {
                node = node.parentNode;
            }
            node.parentNode.replaceChild(spanNode, node);
        }
    }
}
