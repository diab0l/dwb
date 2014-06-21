var util = namespace("util");

script.include("util.js");

var DOMStaticMixin = script.include("static.js");
var DOMCtor   = script.include("ctor.js");

/** 
 * Main function returned by require("lib:dom"); 
 *
 * @name DOM
 * @module
 * @constructs Factory
 * @example 
 * var dom = require("lib:dom");
 *
 * Signal.connect("documentLoaded", function(wv, frame) {
 *      var D = dom(frame);
 * });
 *
 * @param {Object} refObject
 *      Either a WebKitWebView or a WebKitWebFrame
 *
 * @returns {Factory}
 *      The factory function for a {@link Collection}
 *
 * */

var DS = Object.create(null, DOMStaticMixin);

var dom = function(wof) {
   var document = wof.document; 
   var window;
   // only use one reference for DOMWindow because a DOMWindow is never
   // destroyed
   if (wof instanceof WebKitWebView) {
       window = script.getPrivate(wof, "domwindow");
       if (!window) {
           window = document.defaultView;
           script.setPrivate(wof, "domwindow", window);
       }
   }
   /** 
    * The factory function for a {@link Collection}.  Every Factory is bound to
    * the reference document obtained during creation of the factory function, so
    * after a page load a new Factory has to be created
    *
    * @name Factory
    * @class
    *
    * @param {String|Element|Array} selector
    *       A CSS- or XPath-selector that will be used to create the Collection,
    *       or an element that used to create the Collection or an Array of
    *       Elements
    * @param {Node} [node]
    *       The reference node used for the query
    *
    * @returns {Collection}
    *       A new Collection
    * */
   var res = function(selector, node) {
       var collection;
       if (DS.isString(selector)) {
           collection = DS.query(node || document, selector);
       }
       else if (DS.isElement(selector)) {
           collection = [selector];
       }
       else if (DS.isArrayLike(selector)) {
           collection = selector;
       }
       return new DOMCtor(document, collection, selector, window);
   };
   Object.defineProperties(res, DOMStaticMixin);
   /**
    * The reference document 
    *
    * @name document 
    * @memberOf Factory.prototype
    * @readonly 
    * @type HTMLDocument
    * */
   /**
    * The reference window 
    *
    * @name window 
    * @memberOf Factory.prototype
    * @readonly 
    * @type DOMWindow
    * */
   Object.defineProperties(res, { 
       document : { value : document, writable : true }, 
       window : { value : window, writable :true },
   });
   return res;
}

exports =  dom;
// vim: ft=javascript:
