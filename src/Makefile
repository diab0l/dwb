# See COPYING for copyright and license details

BASEDIR=..

REAL_NAME=dwb
COPYRIGHT="(C) 2010-2013 Stefan Bolte"
LICENSE="GNU General Public License, version 3 or later"

include $(BASEDIR)/config.mk
include config.mk

SCRIPTDIR = scripts

SRCSCRIPTS = $(wildcard $(SCRIPTDIR)/*.c)
OBJSCRIPTS = ${SRCSCRIPTS:.c=.o}

CFLAGS+=-I$(BASEDIR)/$(DWB_LIB_DIR_EXAR)
CFLAGS+=-I$(BASEDIR)/$(DWB_LIB_DIR_RC)

CFLAGS += -DNAME=\"$(NAME)\" 
CFLAGS += -DVERSION=\"$(VERSION)\" 
CFLAGS += -DCOPYRIGHT=\"$(COPYRIGHT)\"
CFLAGS += -DLICENSE=\"$(LICENSE)\"
CFLAGS += -DREAL_NAME=\"$(REAL_NAME)\"

DEPS=$(patsubst %.o, %.d, $(OBJ)) 

OBJ += $(BASEDIR)/$(OBJEXAR)
OBJ += $(BASEDIR)/$(OBJRC)
OBJ += $(OBJSCRIPTS)

all: $(TARGET)

$(TARGET): $(OBJ) 
	@echo $(CC) -o $@
	@$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS) 

-include $(OBJ:.o=.d)
-include $(DOBJ:.do=.dd)

%.o: %.c %.h config.h dwb.h
	@echo $(CC) $<
	@$(CC) -c -o $@ $< $(CFLAGS) $(CPPFLAGS) 

debug: $(DTARGET)

deps.d: %.c %.h
	@echo "$(CC) -MM $@"
	@$(CC) $(CFLAGS) -MM $< -o $@


%.do: %.c %.h config.h
	@echo "${CC} $<"
	@$(CC) -c -o $@ $< $(DCFLAGS) 

$(DTARGET): $(DOBJ) ../exar/exar.o ../dwbremote/dwbremote.o
	@echo "$(CC) $@"
	@$(CC) $(DOBJ) ../exar/exar.o ../dwbremote/dwbremote.o -o $(DTARGET) $(LDFLAGS) 

dependencies: $(DEPS)

deps: 
	$(MAKE) -B dependencies

cgdb: debug
	cgdb $(DTARGET)

clean:
	$(RM) *.o  *.do $(TARGET) $(DTARGET) *.d
	$(RM) tlds.h
	$(RM) $(OBJSCRIPTS)

.PHONY: clean all cgdb deps 
