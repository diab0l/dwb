ORIG_CFLAGS := $(CFLAGS)
BASE_DIR=..

REAL_NAME=dwbrc
COPYRIGHT="© 2013 Stefan Bolte"
LICENSE="GNU General Public License, version 3 or later"

include $(BASE_DIR)/version.mk


CFLAGS := -Wall -pedantic -Werror -Wextra -std=c99 -O0 -g
CFLAGS += $(ORIG_CFLAGS)
CFLAGS += -DVERSION=\"$(VERSION)\"
CFLAGS += -DNAME=\"$(NAME)\"
CFLAGS += -DCOPYRIGHT=\"$(COPYRIGHT)\"
CFLAGS += -DLICENSE=\"$(LICENSE)\"


LDFLAGS += -lX11

DCFLAGS += -g -O0 -Wall -pedantic -Werror -Wextra -std=c99 
DCFLAGS += $(ORIG_CFLAGS)

TARGET = dwbrc
OBJ = $(patsubst %.c, %.o, $(wildcard *.c))

SHARED_OBJ = dwbrc.o

all: $(TARGET)

$(TARGET): $(SHARED_OBJ) main.o
	@echo $(CC) -o $@
	@$(CC) $(OBJ) -o $@ $(CFLAGS) $(CPPFLAGS) $(LDFLAGS)

%.o: %.c 
	@echo $(CC) $< 
	@$(CC) -c $< -o $@ $(CFLAGS) $(CPPFLAGS)

debug: 
	make CFLAGS="$(DCFLAGS)"

cgdb: 
	make CFLAGS="$(DCFLAGS)"
	cgdb exar

clean: 
	$(RM) $(OBJ) $(TARGET)

.PHONY: clean
