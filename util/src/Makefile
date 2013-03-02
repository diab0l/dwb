# See COPYING for copyright and license details

BASE_DIR=../..
include $(BASE_DIR)/config.mk
TLDS_H=$(BASE_DIR)/$(SRCDIR)/tlds.h
TLDS_IN=tlds.in
MKTLDS=mktlds-header
MKTLDS_SRC=mktlds-header.c

HSTS=convert_transport_security
HSTS_PRELOAD=$(BASE_DIR)/$(SRCDIR)/hsts_preload.h
TRANSPORT_SECURITY_JSON=transport_security_state_static.json
TRANSPORT_SECURITY_CERTS=transport_security_state_static.certs

OUTFILES=$(HSTS_PRELOAD) $(TLDS_H)
GEN_TOOLS=$(HSTS) $(MKTLDS)

all: $(OUTFILES)

$(TLDS_H): $(TLDS_IN) $(MKTLDS)
	@echo gen $(notdir $@)
	@./$(MKTLDS) < $(TLDS_IN) > $@

$(MKTLDS): $(MKTLDS_SRC)
	@echo "${CC} $<"
	@$(CC) -o $@ $< $(CFLAGS) $(CPPFLAGS) $(LDFLAGS)

$(HSTS_PRELOAD): $(HSTS) $(TRANSPORT_SECURITY_CERTS) $(TRANSPORT_SECURITY_JSON)
	@echo gen $(notdir $@)
	@./$(HSTS) > $@

$(HSTS): $(HSTS).c
	@echo "${CC} $<"
	@$(CC) $(CFLAGS) $(shell pkg-config --cflags --libs glib-2.0 gnutls json) -o $(HSTS) $(HSTS).c

clean: 
	$(RM) $(OUTFILES) $(GEN_TOOLS)

.PHONY: clean
