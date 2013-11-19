(function() {
    var tldEnd = new RegExp("\\.tld$");
    Object.defineProperties(net, {
        /**
         * Checks if two domain matches checking for .tld which matches all top level
         * domains 
         * @name domainMatch
         * @memberOf net 
         * @function 
         *
         * @param {String} domain
         *      The domain to test 
         * @param {String} match 
         *      A domain that may contain .tld as top level domain
         *
         * @returns {Boolean}
         *      Whether the domains match
         * 
         * @example 
         * net.domainMatch("example.com", "example.tld"); // true
         * net.domainMatch("example.com", "example.org"); // false
         * */
        "domainMatch" : 
        {
            value : function(domain, match) {
                var result = false;
                if (tldEnd.test(match))
                {
                    return domain.substring(0, domain.indexOf(".")) === match.substring(0, match.indexOf("."));
                }
                else 
                {
                    return domain === match;
                }
            }
        }, 
        /**
         * Checks if hostnames match checking for .tld which matches all top level
         * domains 
         * @name hostMatch
         * @memberOf net 
         * @function 
         *
         * @param {String} domain
         *      The host to test 
         * @param {String} match 
         *      A host name that may contain .tld as top level domain
         *
         * @returns {Boolean}
         *      Whether the hosts match
         * @example 
         * net.hostMatch("www.example.com", "www.example.tld"); // true
         * net.hostMatch("www.example.com", "example.tld"); // false
         * net.hostMatch("www.example.com", "www.example.com"); // false
         * */
        "hostMatch" : 
        {
            value : function(host, match) {
                if (tldEnd.test(match))
                {
                    var domain = net.domainFromHost(host);
                    if (domain == host)
                        return net.domainMatch(host, match);

                    var domainStart = host.indexOf(domain);
                    if (host.substring(0, domainStart) != match.substring(0, domainStart))
                        return false;

                    return net.domainMatch(host.indexOf(domainStart), match.indexOf(domainStart));
                }
                else 
                {
                    return host == match;
                }
            }
        }, 
    });
    Object.freeze(net);
})();
