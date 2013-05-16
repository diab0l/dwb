# See COPYING for copyright and license details

BASEDIR=..

include $(BASEDIR)/config.mk
include config.mk

CFLAGS+=-I$(BASEDIR)/$(EXARDIR)

DEPS=$(patsubst %.o, %.d, $(OBJ)) 

OBJ += $(BASEDIR)/exar/exar.o

all: $(TARGET)

$(TARGET): $(OBJ) 
	@echo "$(CC) $@"
	@$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS) 

-include $(OBJ:.o=.d)
-include $(DOBJ:.do=.dd)

%.o: %.c %.h config.h dwb.h
	@echo "${CC} $<"
	@$(CC) -c -o $@ $< $(CFLAGS) $(CPPFLAGS) 

debug: $(DTARGET)

deps.d: %.c %.h
	@echo "$(CC) -MM $@"
	@$(CC) $(CFLAGS) -MM $< -o $@


%.do: %.c %.h config.h
	@echo "${CC} $<"
	@$(CC) -c -o $@ $< $(DCFLAGS) 

$(DTARGET): $(DOBJ) 
	@echo "$(CC) $@"
	@$(CC) $(DOBJ) -o $(DTARGET) $(LDFLAGS) 

dependencies: $(DEPS)

deps: 
	$(MAKE) -B dependencies

cgdb: debug
	cgdb $(DTARGET)

clean:
	$(RM) *.o  *.do $(TARGET) $(DTARGET) *.d
	$(RM) tlds.h

.PHONY: clean all cgdb deps 
