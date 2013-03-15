(function () {
    Object.defineProperties(util, 
    { 
        /** 
         * Get the selected text in a webview
         * @name getSelection
         * @memberOf util
         * @function
         *
         * @returns {String} The selected text or null if no text was selected
         * */
        "getSelection" : 
        {
            value : function() 
            {
                var frames = tabs.current.allFrames;
                for (var i=frames.length-1; i>=0; --i) 
                {
                    var selection = JSON.parse(frames[i].inject("return document.getSelection().toString()"));
                    if (selection.length > 0)
                        return selection;
                }
                return null;
            }
        },
        /** 
         * Converts camel-case string for usage with GObject properties to a
         * non-camel-case String
         * @name uncamelize 
         * @memberOf util 
         * @function
         *
         * @returns {String} The uncamelized String
         * */
        "uncamelize" : 
        {
            value : function(text) 
            {
                if (! text || text.length === 0)
                    return text;
                return text.replace(/(.)?([A-Z])/g, function(x, s, m) { 
                    return s ? s + "-" + m.toLowerCase() : m.toLowerCase(); 
                });
            }
        },
        /** 
         * Converts non-camel-case string to a camel-case string
         * non-camel-case String
         *
         * @name camelize 
         * @memberOf util 
         * @function
         *
         * @returns {String} A camelcase String
         * */
        "camelize" : 
        {
            value : function(text) 
            {
                if (! text || text.length === 0)
                    return text;
                return text.replace(/[-_]+(.)?/g, function(a, b) { 
                    return b ? b.toUpperCase() : ""; 
                });
            }
        }
    });
    Object.freeze(util);
    
    if (Object.prototype.forEach === undefined) 
    {
        Object.defineProperty(Object.prototype, "forEach", 
        { 
            value : function (callback) 
            {
                var key;
                for (key in this) 
                {
                    if (this.hasOwnProperty(key))
                        callback(key, this[key], this); 
                }
            }
        });
    }
})();
