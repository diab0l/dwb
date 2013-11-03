(function() {
    Object.defineProperties(system, {
        "spawn" : 
        {
            value : (function() {
                return function(command, detail) {
                    detail = detail || {};
                    var onStdout = detail.onStdout, 
                        onStderr = detail.onStderr, 
                        d = new Deferred();
                    var result = {
                        stdout : "", 
                        stderr : "", 
                        status : 0
                    };
                    if (typeof detail === "function")
                    {
                        io.print("\033[31;1mDWB DEPRECATION:\033[0m Using system.spawn(command, onStdout, onStderr); is deprecated!");
                        onStdout = arguments[1];
                        onStderr = arguments[2];
                        detail.stdin = arguments[3];
                        detail.environment = arguments[4];
                    }
                       
                    system._spawn(command, 
                        function(response) {
                            result.stdout += response;
                            if (onStdout)
                            {
                                onStdout(response);
                            }
                        }, 
                        function(response) {
                            result.stderr += response;
                            if (onStderr)
                            {
                                onStderr(response);
                            }

                    }, detail.stdin, detail.environment).then(function(status) {
                                result.status = status;
                                if (detail.onFinished)
                                {
                                    detail.onFinished(result);
                                }
                                d.resolve(result);
                            }, 
                            function(status) {
                                result.status = status;
                                if (detail.onFinished)
                                {
                                    detail.onFinished(result);
                                }
                                d.reject(result);
                            }

                    );
                    return d;
                };
            })()
        }
    });
    Object.freeze(system);
})();
