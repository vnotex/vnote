/*! markdown-it-headinganchor 1.2.1 https://github.com//adam-p/markdown-it-headinganchor @license MIT */(function(f){if(typeof exports==="object"&&typeof module!=="undefined"){module.exports=f()}else if(typeof define==="function"&&define.amd){define([],f)}else{var g;if(typeof window!=="undefined"){g=window}else if(typeof global!=="undefined"){g=global}else if(typeof self!=="undefined"){g=self}else{g=this}g.markdownitHeadingAnchor = f()}})(function(){var define,module,exports;return (function e(t,n,r){function s(o,u){if(!n[o]){if(!t[o]){var a=typeof require=="function"&&require;if(!u&&a)return a(o,!0);if(i)return i(o,!0);var f=new Error("Cannot find module '"+o+"'");throw f.code="MODULE_NOT_FOUND",f}var l=n[o]={exports:{}};t[o][0].call(l.exports,function(e){var n=t[o][1][e];return s(n?n:e)},l,l.exports,e,t,n,r)}return n[o].exports}var i=typeof require=="function"&&require;for(var o=0;o<r.length;o++)s(r[o]);return s})({1:[function(require,module,exports){
/*
 * Copyright Adam Pritchard 2015
 * MIT License : http://adampritchard.mit-license.org/
 */

'use strict';
/*jshint node:true*/

function slugify(md, s) {
  // Unicode-friendly
  var spaceRegex = new RegExp(md.utils.lib.ucmicro.Z.source, 'g');
  return encodeURIComponent(s.replace(spaceRegex, ''));
}

function makeRule(md, options) {
  return function addHeadingAnchors(state) {
    // Go to length-2 because we're going to be peeking ahead.
    for (var i = 0; i < state.tokens.length - 1; ++i) {
      if (state.tokens[i].type !== 'heading_open' ||
          state.tokens[i+1].type !== 'inline') {
        continue;
      }

      var headingOpenToken = state.tokens[i];
      var headingInlineToken = state.tokens[i+1];

      if (!headingInlineToken.content) {
        continue;
      }

      var anchorName = options.slugify(md, headingInlineToken.content);

      options.headingHook(headingOpenToken, headingInlineToken, anchorName);

      if (options.addHeadingID) {
        headingOpenToken.attrPush(['id', anchorName]);
      }

      if (options.addHeadingAnchor) {
        var anchorToken = new state.Token('html_inline', '', 0);
        if (options.addHeadingID) {
          // No need to add id in anchor.
          anchorToken.content = '<a class="' + options.anchorClass + '" ' +
                                'href="#' + anchorName + '" ' +
                                'data-anchor-icon="' + options.anchorIcon + '" ' +
                                '></a>';
        } else {
          anchorToken.content = '<a id="' + anchorName + '" ' +
                                'class="' + options.anchorClass + '" ' +
                                'href="#' + anchorName + '" ' +
                                'data-anchor-icon="' + options.anchorIcon + '" ' +
                                '></a>';
        }

        headingInlineToken.children.push(anchorToken);
      }

      // Advance past the inline and heading_close tokens.
      i += 2;
    }
  };
}

module.exports = function headinganchor_plugin(md, opts) {
  var defaults = {
    anchorClass: 'markdown-it-headinganchor',
    addHeadingID: true,
    addHeadingAnchor: true,
    // Added by Le Tan (github.com/tamlok)
    anchorIcon: '#',
    slugify: slugify,
    headingHook: function(openToken, inlineToken, anchor) {}
  };
  var options = md.utils.assign(defaults, opts);
  md.core.ruler.push('heading_anchors', makeRule(md, options));
};

},{}]},{},[1])(1)
});
