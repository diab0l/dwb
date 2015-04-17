var domutil = require("dom$util");

function getClass(instance) {
    var s = Object.prototype.toString.call(instance);
    return s.substring(8, s.length - 1);
}

var DOMStaticMixin = {
    /**
     * Test whether an object is a string
     *
     * @name isString
     * @memberOf Factory.prototype
     * @function
     * @param {Object} object
     *      The object to test
     *
     * @returns {Boolean}
     *
     * */
    isString : { 
        value : function (s) { return getClass(s) == 'String'; }
    },
    /**
     * Test whether an object is a {@link Collection}
     *
     * @name isCollection
     * @memberOf Factory.prototype
     * @function
     * @param {Object} object
     *      The object to test
     *
     * @returns {Boolean}
     *
     * */
    isCollection : {
        value : function (d) { return d instanceof DOMCtor; }
    },
    /**
     * Test whether an object is a DOMElement
     *
     * @name isElement
     * @memberOf Factory.prototype
     * @function
     * @param {Object} object
     *      The object to test
     *
     * @returns {Boolean}
     *
     * */
    isElement : {
        value : function (e) { return e && e.nodeType == 1; } 
    },
    create : {
        value : function(name) {
            return this.document.createElement(name);
        } 
    },
    /**
     * Test whether an object is a DOMObject
     *
     * @name isDomObject
     * @memberOf Factory.prototype
     * @function
     * @param {Object} object
     *      The object to test
     *
     * @returns {Boolean}
     *
     * */
    isDomObject : {
        value : function (o) { return o.toString() == "[object DOMObject]"; },
    },
    /**
     * Test whether an object is an array-like object
     *
     * @name isArrayLike
     * @memberOf Factory.prototype
     * @function
     * @param {Object} object
     *      The object to test
     *
     * @returns {Boolean}
     *
     * */
    isArrayLike : {
        value : function (a) { return typeof a.length == "number"; } 
    }, 
    /**
     * Queries for dom nodes
     *
     * @name query
     * @memberOf Factory.prototype
     * @function
     * @param {Node} refNode
     *      The reference node
     * @param {String} selector
     *      Either a css selector or an XPath description. XPath descriptions
     *      must start with <i>XPath:</i>
     *
     * @returns {Array[Node]}
     *
     * */
    query : {
        value : function(node, selector) {
            var result = [];
            if (domutil.startsWith(selector, "XPATH:")) {
                node = node.nodeType == 9 ? node.documentElement : node;
                result = node.ownerDocument.evaluate(node, selector.substring(6));
            }
            else {
                result = node.querySelectorAll(selector);
            }
            return result;
        }
    }, 
    /**
     * Gets an element by id
     *
     * @name byId
     * @memberOf Factory.prototype
     * @function
     * @param {String} id
     * @param {Element} [refnode]
     *      The reference node
     *
     * @returns {Element}
     *
     * */
    byId : {
        value : function(id, node) {
            return (node || this.document).querySelector("#" + id);
        }
    },
    /**
     * The currently focused element
     *
     * @name focus
     * @memberOf Factory.prototype
     * @type Element
     *
     * */
    focus : { 
        get : function() { 
            return this.document.querySelector("*:focus"); 
        }, 
        set : function(e) {
            e.focus();
        }

    }, 
    /**
     * Sets or gets css properties of an element. If css is defined it sets the
     * properties, otherwise it gets the properties
     *
     * @name css
     * @memberOf Factory.prototype
     * @function
     *
     * @param {Element} element
     * @param {Object}  [css]
     *      A hash of properties to set. If a property value evaluates to false
     *      (e.g. "", null, undefined) the style property is deleted from the
     *      element
     *      
     * @returns {Object|Element}
     *      If css is defined it returns the element, otherwise it returns a
     *      hash containing all css properties defined on that element
     * */
    css : {
        value : function(e, props) {
            var key, cssName;

            var style = this.css2obj(e.style.cssText);
            if (!props) {
                return style;
            }
            for (key in props) {
                if (props[key]) {
                    style[key] = props[key];
                }
                else {
                    delete style[key];
                }
            }
            e.style.cssText = this.obj2css(style);
            return e;
        }
    }, 
    /**
     * Converts a hash of css properties to a css string that can be used as cssText of
     * elements styles.
     *
     * @name obj2css
     * @memberOf Factory.prototype
     * @function
     *
     * @param {Object} props
     *      
     * @returns {String}
     * */
    obj2css : {
        value : function(o) {
            var css = "", key, cssName;
            for (key in o) {
                cssName = util.uncamelize(key);
                if (domutil.startsWith(cssName, "webkit-")) {
                    cssName = "-" + cssName;
                }
                css += cssName + ": " + o[key] + "; ";
            }
            return css;
        }
    }, 
    /**
     * Converts a css string to a hash of properties. 
     *
     * @name css2obj
     * @memberOf Factory.prototype
     * @function
     *
     * @param {String} css
     *      
     * @returns {Object}
     * */
    css2obj : {
        value : function(css) {
            var style = {};
            if (!css.trim()) {
                return style;
            }

            css.split(";").map(function(rule) {
                return rule.trim();
            }).filter(function(rule) {
                return rule.length > 0;
            }).forEach(function(rule) {
                var map = rule.split(":");
                var cssName = map[0].trim();
                if (domutil.startsWith(cssName, "-")) {
                    cssName = cssName.substring(1);
                }
                style[util.camelize(cssName)] = map[1].trim();
            });
            return style;
        }
    }, 
    /**
     * Gets the computed style as an hash of style properties
     *
     * @name computedStyle
     * @memberOf Factory.prototype
     * @function
     *
     * @param {Element} element
     *      
     * @returns {Object}
     * */
    computedStyle : {
        value : function(e) {
            var s = this.window.getComputedStyle(e);
            return this.css2obj(s.cssText);
        }
    },
    document: { get: domutil.getDocument }, 
    window: { 
      get: function() {
        return domutil.getWindow(tabs.current);
      } 
    }
};
provide("dom$static", DOMStaticMixin);
exports = DOMStaticMixin;
