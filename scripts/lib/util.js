(function () {
    Object.defineProperties(util, 
    { 
        "getBody" :  
        { 
            value :  function(f) 
            {
                if (f && f instanceof Function) 
                {
                    var m = f.toString().match(/\{([\s\S]*)\}/m)[1];
                    m = m.replace(/^[ \t\r\v\f]*/, '');
                    if (m[0] == "\n")
                        return m.substring(1);
                    else 
                        return m;
                }
                return null;
            }
        },
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
    if (Array.prototype.fastIndexOf === undefined) 
    {
        Object.defineProperty(Array.prototype, "fastIndexOf", 
        {
            value : function (v) 
            {
                for (var i=0, l=this.length; i<l; ++i) {
                    if (this[i] == v)
                        return i;
                }
                return -1;
            }
        });
    }
    if (Array.prototype.fastLastIndexOf === undefined) 
    {
        Object.defineProperty(Array.prototype, "fastLastIndexOf", 
        {
            value : function (v) 
            {
                for (var i=this.length-1; i>=0; --i) {
                    if (this[i] == v)
                        return i;
                }
                return -1;
            }
        });
    }
    if (! RegExp.escape)
    {
        Object.defineProperty(RegExp, "escape", 
        {
            value : function(string)
            {
                return string.replace(/[-\/\\^$*+?.()|[\]{}]/g, '\\$&');
            }
        });
    }
})();
