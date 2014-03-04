/*jslint eqeq:true*/
/*global system,FileTest,extensions,io,bind,tabs,script,xprovide,xrequire*/
var config = xrequire("config");

var shared = {
    bindHandles : {},
    getAll : function() {
        var data, backup, content;
        if (system.fileTest(config.file, FileTest.exists)) {
            content = io.read(config.file);
            try {
                data = JSON.parse(content);
            }
            catch (e) {
                backup = config.file + ".backup";
                extensions.error(config.name, "The bookmarklet file is corrupted, creating backup " + backup);
                io.write(backup, "w", content);
            }
        }
        return data || {};
    },
    addBind : function(name, bm) {
        if (bm.shortcut || bm.command) {
            var handle = bind(bm.shortcut || null, 
                            this.inject.bind(null, bm.script), 
                            bm.command || null);
            this.bindHandles[name] = handle;
            script.own(handle);
        }
    }, 
    inject : function(bm, cmd) {
       (cmd && cmd.nummod != -1 ? tabs[cmd.nummod - 1] : tabs.current).inject(bm, null, 0);
    },
    delete : function(data, name) {
       data = data || this.getAll();
       delete data[name];
       this.save(data);
       this.removeBind(name);
    },
    removeAllBinds : function() {
       var name;
       for (name in this.bindHandles) {
           this.removeBind(name);
       }
    },
    removeBind : function(name) {
       if (this.bindHandles[name]) {
           this.bindHandles[name].remove();
           delete this.bindHandles[name];
       }
    },
    addAllBinds : function() {
        var name;
        var bookmarklets = this.getAll();
        for (name in bookmarklets) {
            this.addBind(name, bookmarklets[name]);
        }
    },
    save : function(data) {
        io.write(config.file, "w", JSON.stringify(data));
    },
    end : function() {
       this.bindHandles = {};
       script.removeHandles();
    }
};
xprovide("shared", shared, true);

return shared;
