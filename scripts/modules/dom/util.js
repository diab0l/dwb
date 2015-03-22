var domutil = { 
    startsWith : function (str, prefix) {
        return str.substring(0, prefix.length) == prefix;
    }, 
    off : function (events) {
        events.forEach(function(e) { e.remove(); });
    }, 
    getDocument: function() {
      return tabs.current.document;
    }, 
    getWindow: function(wv) {
      var window = script.getPrivate(wv, "domwindow");
      if (!window) {
        window = wv.document.defaultView;
        script.setPrivate(wv, "domwindow", window);
      }
      return window;
    }
};
provide("dom$util", domutil);

exports = domutil;
