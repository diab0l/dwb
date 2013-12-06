// See COPYING for copyright and license details

(function () {
  var _config = {};
  var _registered = {};
  var _configLoaded = false;

  function _getPlugin(name, filename) 
  {
      if (system.fileTest(filename, FileTest.exists)) 
          return include(filename);
      else if (system.fileTest(filename + ".exar", FileTest.exists))
          return include(filename + ".exar");
      return null;
  };
  function _getStack(offset) 
  {
      if (arguments.length === 0) 
          offset = 0;

      try 
      {
          throw Error (message);
      }
      catch (e) 
      {
          var stack = e.stack.match(/[^\n]+/g);
          return "STACK: [" + stack.slice(offset+1).join("] [")+"]";
      }
  };
  function _unload(name, removeConfig) 
  {
      if (_registered[name] !== undefined) 
      {
          if (typeof _registered[name].end == "function") 
          {
              _registered[name].end();
              extensions.message(name, "Extension unloaded.");
          }
          if (removeConfig)
              delete _config[name];

          delete _registered[name];

          provide(name, null, true);

          return true;
      }
      return false;
  };
  Object.defineProperties(extensions, 
  { 
      "warning" : 
      {
          value : function (name, message) 
          {
              io.print("\033[1mDWB EXTENSION WARNING: \033[0mextension  \033[1m" + name + "\033[0m: " + message, "stderr");
          }
      }, 
      "error" : 
      {
          value : function (name, a, b) {
              var message = "";
              if (a instanceof Error) {
                  if (a.message) {
                      message = a.message;
                  }
                  else if (arguments.length > 2)
                  message = b;
                  else 
                      b = "";
                  io.print("\033[31mDWB EXTENSION ERROR: \033[0mextension \033[1m" + name + "\033[0m in line " + a.line + ": " + 
                          message + "\nSTACK: [" + a.stack.match(/[^\n]+/g).join("] [") + "]", "stderr");
              }
              else {
                  io.print("\033[31mDWB EXTENSION ERROR: \033[0mextension \033[1m" + name + "\033[0m: " + a + "\n" + _getStack(1), "stderr");
              }
          }
      },
      "message" : 
      {
          value : function (name, message) 
          {
              io.print("\033[1mDWB EXTENSION: \033[0mextension \033[1m" + name + "\033[0m: " + message, "stderr");
          }
      },
      "load" : 
      {
          value : function(name, c) 
          {
              if (_registered[name] !== undefined) 
                  extensions.error(name, "Already loaded.");

              var config, plugin = null, key, filename;
              var extConfig = null;

              if (arguments.length == 2) 
              {
                  extConfig = c;
                  _config[name] = c;
              }
              if (!_configLoaded) 
              {
                  if (system.fileTest(data.configDir + "/extensionrc", FileTest.regular)) 
                  {
                      try 
                      {
                          config = include(data.configDir + "/extensionrc");
                      }
                      catch (e) 
                      {
                          extensions.error(name, "loading config failed : " + e);
                      }
                      if (config === null) 
                      {
                          extensions.warning(name, "Could not load config.");
                      }
                      else 
                      {
                          for (key in config) 
                              _config[key] = config[key];
                      }
                      _configLoaded = true;
                  }
              }
              if (extConfig === null) 
                  extConfig = _config[name] || null;

              if (data.userDataDir) 
              {
                  filename = data.userDataDir + "/extensions/" + name;
                  plugin = _getPlugin(name, data.userDataDir + "/extensions/" + name);
              }
              if (plugin === null) 
              {
                  plugin = _getPlugin(name, data.systemDataDir + "/extensions/" + name);
                  if (plugin === null) 
                  {
                      extensions.error(name, "Couldn't find extension.");
                      return;
                  }
              }
              if (plugin === undefined || typeof plugin.init != "function")
              {
                  extensions.warning(name, "Missing initializer");
                  return;
              }
              try 
              {
                  plugin._name = name;

                  if (plugin.apiVersion && plugin.apiVersion > version)
                  {
                      extensions.error(name, "Required API-Version: \033[1m" + plugin.apiVersion + 
                                       "\033[0m, API-Version found: \033[1m" + version + "\033[0m");
                      return;
                  }

                  if (plugin.defaultConfig) 
                      util.mixin(extConfig, plugin.defaultConfig);

                  Deferred.when(plugin.init(extConfig), function(success) {
                      if (success)
                      {
                          _registered[name] = plugin;

                          if (plugin.exports) 
                              provide(name, plugin.exports, true);

                          extensions.message(name, "Successfully loaded and initialized.");
                      }
                      else 
                      {
                          extensions.error(name, "Initialization failed.");
                      }

                  }, function(reason) {
                     if (reason)
                         extensions.error(name, "Initialization failed: " + reason);
                     else 
                         extensions.error(name, "Initialization failed.");
                  });
              }
              catch (e) 
              {
                  extensions.error(name, "Initialization failed: " + e);
              }
          }
      },
      "unload" : 
      { 
          value : function(name) 
          {
              return _unload(name, true);
          }
      }, 
      "disableAll" : 
      {
          value : function()
          {
              for (var key in _registered) 
                  _unload(key, true);
          }
      }, 
      "toggle" : 
      {
          value : function(name, c) 
          {
              if (_registered[name] !== undefined) 
              {
                  _unload(name);
                  return false;
              }
              else 
              {
                  extensions.load(name, c);
                  return true;
              }
          }
      },
      "bind" : 
      {
          value : function(name, shortcut, options) 
          {
              if (!name || !shortcut)
                  return;

              if (options.load === undefined || options.load) 
                  extensions.load(name, options.config);

              bind(shortcut, function () { 
                  if (extensions.toggle(name, options.config)) 
                      io.notify("Extension " + name + " enabled");
                  else 
                      io.notify("Extension " + name + " disabled");
              }, options.command);
          }
      }
  });
})();
Object.freeze(extensions);
