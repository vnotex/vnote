/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Stefan Goessner - 2017-18. All rights reserved.
 *  Licensed under the MIT License. See License.txt in the project root for license information.
 *  Modified by Le Tan for MathJax support in VNote.
 *  We mark all the formulas and enclose them with $ in class 'tex-to-render' for
 *  further processing by MathJax.
 *--------------------------------------------------------------------------------------------*/
'use strict';

function texmath(md, options) {
    let delimiters = options && options.delimiters || 'dollars';

    if (delimiters in texmath.rules) {
        for (let rule of texmath.rules[delimiters].inline) {
            md.inline.ruler.before('escape', rule.name, texmath.inline(rule));  // ! important
            md.renderer.rules[rule.name] = (tokens, idx) => rule.tmpl.replace(/\$1/,texmath.render(tokens[idx].content,false));
        }

        for (let rule of texmath.rules[delimiters].block) {
            md.block.ruler.before('fence', rule.name, texmath.block(rule));
            md.renderer.rules[rule.name] = (tokens, idx) => rule.tmpl.replace(/\$1/,texmath.render(tokens[idx].content,true));
        }
    }
}

texmath.applyRule = function(rule, str, beg) {
    let pre, match, post;
    rule.rex.lastIndex = beg;
    pre = str.startsWith(rule.tag,beg) && (!rule.pre || rule.pre(str,beg));
    match = pre && rule.rex.exec(str);
    if (match) {
        match.lastIndex = rule.rex.lastIndex;
        post = !rule.post || rule.post(str, match.lastIndex-1);
    }
    rule.rex.lastIndex = 0;
    return post && match;
}

texmath.inline = (rule) =>
    function(state, silent) {
        let res = texmath.applyRule(rule, state.src, state.pos);
        if (res) {
            if (!silent) {
                let token = state.push(rule.name, 'math', 0);
                token.content = res[1];  // group 1 from regex ..
                token.markup = rule.tag;
            }
            state.pos = res.lastIndex;
        }
        return !!res;
    }

texmath.block = (rule) =>
    function(state, begLine, endLine, silent) {
        let res = texmath.applyRule(rule, state.src, state.bMarks[begLine] + state.tShift[begLine]);
        if (res) {
            if (!silent) {
                let token = state.push(rule.name, 'math', 0);
                token.block = true;
                token.content = res[1];
                token.markup = rule.tag;
            }
            for (let line=begLine, endpos=res.lastIndex-1; line < endLine; line++)
                if (endpos >= state.bMarks[line] && endpos <= state.eMarks[line]) { // line for end of block math found ...
                    state.line = line+1;
                    break;
                }
            state.pos = res.lastIndex;
        }
        return !!res;
    }

texmath.render = function(tex, isblock) {
    let res;
    if (isblock) {
        res = '$$$$' + tex + '$$$$';
    } else {
        res = '$$' + tex + '$$';
    }

    return res;
}

texmath.$_pre = (str,beg) => {
    let prv = beg > 0 ? str[beg-1].charCodeAt(0) : false;
    return !prv || prv !== 0x5c                // no backslash,
                && (prv < 0x30 || prv > 0x39); // no decimal digit .. before opening '$'
}
texmath.$_post = (str,end) => {
    let nxt = str[end+1] && str[end+1].charCodeAt(0);
    return !nxt || nxt < 0x30 || nxt > 0x39;   // no decimal digit .. after closing '$'
}

texmath.rules = {
    brackets: {
        inline: [
            {   name: 'math_inline',
                rex: /\\\((.+?)\\\)/gy,
                tmpl: '<x-eq class="tex-to-render">$1</x-eq>',
                tag: '\\('
            }
        ],
        block: [
            {   name: 'math_block',
                rex: /\\\[(.+?)\\\]\s*$/gmy,
                tmpl: '<x-eqn class="tex-to-render">$1</x-eqn>',
                tag: '\\['
            }
        ]
    },
    gitlab: {
        inline: [
            {   name: 'math_inline',
                rex: /\$`(.+?)`\$/gy,
                tmpl: '<x-eq class="tex-to-render">$1</x-eq>',
                tag: '$`'
            }
        ],
        block: [
            {   name: 'math_block',
                rex: /`{3}math\s+?([^`]+?)\s+?`{3}\s*$/gmy,
                tmpl: '<x-eqn class="tex-to-render">$1</x-eqn>',
                tag: '```math'
            }
        ]
    },
    dollars: {
        inline: [
            {   name: 'math_inline',
                rex: /\$(\S[^$\r\n]*?[^\s\\]{1}?)\$/gy,
                tmpl: '<x-eq class="tex-to-render">$1</x-eq>',
                tag: '$',
                pre: texmath.$_pre,
                post: texmath.$_post
            },
            {   name: 'math_single',
                rex: /\$([^$\s\\]{1}?)\$/gy,
                tmpl: '<x-eq class="tex-to-render">$1</x-eq>',
                tag: '$',
                pre: texmath.$_pre,
                post: texmath.$_post
            }
        ],
        block: [
            {   name: 'math_block',
                rex: /\${2}([^$]*?)\${2}\s*$/gmy,
                tmpl: '<x-eqn class="tex-to-render">$1</x-eqn>',
                tag: '$$'
            }
        ]
    },
};

if (typeof module === "object" && module.exports)
   module.exports = texmath;
