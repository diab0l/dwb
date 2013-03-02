# See COPYING for copyright and license details

include ../config.mk
SETTINGS=../$(LIBDIR)/settings.html
KEYS=../$(LIBDIR)/keys.html
OUTFILES=$(SETTINGS) $(KEYS) 

all: $(OUTFILES)

$(KEYS): keys.m4
	@echo gen keys.html
	@m4 $(M4FLAGS) $< > $@

$(SETTINGS): settings.m4
	@echo gen settings.html
	@m4 $(M4FLAGS) $< > $@

settings.in: settings.pre
	@$(shell if pkg-config --exists 'libsoup-2.4 >= 2.38'; then \
		sed 's/^SSL_CERTIFICATION/ssl-use-system-ca-file checkbox Whether to use the system certification file/' $< > $@;\
		else  sed 's/^SSL_CERTIFICATION/ssl-ca-file text Path to ssl-certificate/;/^addressbar-dns-lookup/d' $< > $@; fi)

clean: 
	$(RM) $(OUTFILES) settings.in

.PHONY: clean
