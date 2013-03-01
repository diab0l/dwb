# See COPYING for copyright and license details

include ../config.mk
SETTINGS=../$(LIBDIR)/settings.html
KEYS=../$(LIBDIR)/keys.html
TLDS_H=../$(SRCDIR)/tlds.h
HSTS=convert_transport_security
HSTS_PRELOAD=../$(SRCDIR)/hsts_preload.h
OUTFILES=$(SETTINGS) $(KEYS) $(HSTS_PRELOAD) $(TLDS_H)

all: $(OUTFILES)

$(KEYS): keys.in
	@echo gen keys.html
	@awk -f generate_keys.awk $< > $@

$(SETTINGS): settings.in
	@echo gen settings.html
	@awk -f generate_settings.awk $< > $@

$(TLDS_H): ../src/tlds.in mktlds-header
	@echo gen $@
	@./mktlds-header < ../src/tlds.in > $@

mktlds-header: mktlds-header.o
	@echo "${CC} $<"
	@$(CC) -o $@ $< $(CFLAGS) $(CPPFLAGS) $(LDFLAGS)

%.o: %.c
	@echo "${CC} $<"
	@$(CC) -c -o $@ $< $(CFLAGS) $(CPPFLAGS) 

settings.in: settings.pre
	@$(shell if pkg-config --exists 'libsoup-2.4 >= 2.38'; then \
		sed 's/^SSL_CERTIFICATION/ssl-use-system-ca-file checkbox Whether to use the system certification file/' $< > $@;\
		else  sed 's/^SSL_CERTIFICATION/ssl-ca-file text Path to ssl-certificate/;/^addressbar-dns-lookup/d' $< > $@; fi)

$(HSTS): $(HSTS).c
	@echo "${CC} $<"
	@$(CC) $(CFLAGS) $(shell pkg-config --cflags --libs glib-2.0 gnutls json) -o $(HSTS) $(HSTS).c

$(HSTS_PRELOAD): $(HSTS) transport_security_state_static.certs transport_security_state_static.json
	./$(HSTS) > $(HSTS_PRELOAD)

clean: 
	$(RM) $(OUTFILES)

.PHONY: clean
