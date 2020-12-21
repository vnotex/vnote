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
            CrossCopy.rule_replaceLocalImageWithLabel
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
            CrossCopy.rule_replaceLocalImageWithLabel,
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

    crossCopy(p_id, p_timeStamp, p_target, p_baseUrl, p_html) {
        let target = this.targets[p_target];
        if (!target) {
            console.error("no matching cross-copy target", p_target);
            this.adapter.setCrossCopyResult(p_id, p_timeStamp, p_html);
        }

        let info = {
            inst: this,
            baseUrl: p_baseUrl
        };

        let result = this.executeRules(target.rules, info, p_html);
        this.adapter.setCrossCopyResult(p_id, p_timeStamp, result);
    }

    executeRules(p_rules, p_info, p_html) {
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
            p_rules[i](p_info, doc);
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
            if (window.vnotex.os === "Windows") {
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

    // TODO: if we deploy a http server, can we eliminate this tricky rule?
    static rule_replaceLocalImageWithLabel(p_info, p_doc) {
        let imgs = p_doc.getElementsByTagName('img');
        for (let i = 0; i < imgs.length; ++i) {
            let img = imgs[i];
            let httpRegExp = new RegExp('^http[s]://');
            if (httpRegExp.test(img.src)) {
                continue;
            }

            let dataRegExp = new RegExp('^data:image/');
            if (dataRegExp.test(img.src)) {
                continue;
            }

            let spanNode = p_doc.createElement('span');
            spanNode.style = 'font-weight: bold; color: white; background-color: red;'
            spanNode.textContent = 'INSERT_IMAGE_HERE';
            img.parentNode.replaceChild(spanNode, img);
            --i;
        }
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
