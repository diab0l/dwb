(function() {
    function execute(f, arg)
    {
        tabs.current.inject("var a=JSON.parse(arguments[0]);console." + f + "(a);", 
            JSON.stringify(arg));
    }
    Object.defineProperties(console, 
    {
        /**
         * @name assert
         * @memberOf console
         * @function
         * @param {Expression} Expression to test
         * */
        "assert" : { value : execute.bind(null, "assert") },
        /**
         * @name count
         * @memberOf console
         * @function
         * @param {String} Name
         * */
        "count" : { value : execute.bind(null, "count") },
        /**
         * @name debug
         * @memberOf console
         * @function
         * @param {Object} argument Argument passed to real function
         * */
        "debug" : { value : execute.bind(null, "debug") },
        /**
         * @name error
         * @memberOf console
         * @function
         * @param {Object} argument Argument passed to real function
         * */
        "error" : { value : execute.bind(null, "error") },
        /**
         * @name group
         * @memberOf console
         * @function
         * @param {Object} argument Argument passed to real function
         * */
        "group" : { value : execute.bind(null, "group") },
        /**
         * @name groupCollapsed
         * @memberOf console
         * @function
         * @param {Object} argument Argument passed to real function
         * */
        "groupCollapsed" : { value : execute.bind(null, "groupCollapsed") },
        /**
         * @name groupEnd
         * @memberOf console
         * @function
         * @param {Object} argument Argument passed to real function
         * */
        "groupEnd" : { value : execute.bind(null, "groupEnd") },
        /**
         * @name info
         * @memberOf console
         * @function
         * @param {Object} argument Argument passed to real function
         * */
        "info" : { value : execute.bind(null, "info") },
        /**
         * @name log
         * @memberOf console
         * @function
         * @param {Object} argument Argument passed to real function
         * */
        "log" : { value : execute.bind(null, "log") },
        /**
         * @name time
         * @memberOf console
         * @function
         * @param {Object} argument Argument passed to real function
         * */
        "time" : { value : execute.bind(null, "time") },
        /**
         * @name timeEnd
         * @memberOf console
         * @function
         * @param {String} name Argument passed to real function
         * */
        "timeEnd" : { value : execute.bind(null, "timeEnd") },
        /**
         * @name warn
         * @memberOf console
         * @function
         * @param {String} name Argument passed to real function
         * */
        "warn" : { value : execute.bind(null, "warn") }
    });
    Object.freeze(console);
})();
