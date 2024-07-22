(function(f){if(typeof exports==="object"&&typeof module!=="undefined"){module.exports=f()}else if(typeof define==="function"&&define.amd){define([],f)}else{var g;if(typeof window!=="undefined"){g=window}else if(typeof global!=="undefined"){g=global}else if(typeof self!=="undefined"){g=self}else{g=this}g.markdownItXSS = f()}})(function(){var define,module,exports;return (function e(t,n,r){function s(o,u){if(!n[o]){if(!t[o]){var a=typeof require=="function"&&require;if(!u&&a)return a(o,!0);if(i)return i(o,!0);var f=new Error("Cannot find module '"+o+"'");throw f.code="MODULE_NOT_FOUND",f}var l=n[o]={exports:{}};t[o][0].call(l.exports,function(e){var n=t[o][1][e];return s(n?n:e)},l,l.exports,e,t,n,r)}return n[o].exports}var i=typeof require=="function"&&require;for(var o=0;o<r.length;o++)s(r[o]);return s})({1:[function(require,module,exports){
'use strict';

module.exports = function protect_xss(md, opts = {}) {
  const proxy = (tokens, idx, options, env, self) => self.renderToken(tokens, idx, options);
  const defaultHtmlInlineRenderer = md.renderer.rules.html_inline || proxy;
  const defaultHtmlBlockRenderer = md.renderer.rules.html_block || proxy;
  opts.whiteList = {...window.filterXSS.getDefaultWhiteList(), ...opts.whiteList};
  // Do not escape value when it is a tag and attr in the whitelist.
  opts.safeAttrValue = (tag, name, value, cssFilter) => { return value; }

  function protectFromXSS(html) {
    return filterXSS(html, opts);
  }

  function filterContent(tokens, idx, options, env, slf, fallback) {
    tokens[idx].content = protectFromXSS(tokens[idx].content);
    return fallback(tokens, idx, options, env, slf);
  }

  md.renderer.rules.html_inline = (tokens, idx, options, env, slf) =>
    filterContent(tokens, idx, options, env, slf, defaultHtmlInlineRenderer);
  md.renderer.rules.html_block = (tokens, idx, options, env, slf) =>
    filterContent(tokens, idx, options, env, slf, defaultHtmlBlockRenderer);
};

},{}]},{},[1])(1)
});
