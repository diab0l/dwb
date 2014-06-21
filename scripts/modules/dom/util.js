var domutil = { 
    startsWith : function (str, prefix) {
        return str.substring(0, prefix.length) == prefix;
    }, 
    off : function (events) {
        events.forEach(function(e) { e.remove(); });
    }
};
provide("dom$util", domutil);
