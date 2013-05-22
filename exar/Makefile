ORIG_CFLAGS := $(CFLAGS)

CFLAGS := -Wall -pedantic -Werror -Wextra -std=c99 -Os
CFLAGS += $(ORIG_CFLAGS)

DCFLAGS += -g -O0 -Wall -pedantic -Werror -Wextra -std=c99 
DCFLAGS += $(ORIG_CFLAGS)

TARGET=exar
OBJ=$(patsubst %.c, %.o, $(wildcard *.c))

all: $(TARGET)

$(TARGET): $(OBJ)
	@echo $(CC) -o $@
	@$(CC) $(OBJ) -o $@ $(CFLAGS) $(CPPFLAGS)

%.o: %.c 
	@echo $(CC) $< 
	@$(CC) -c $< -o $@ $(CFLAGS) $(CPPFLAGS)

debug: 
	make CFLAGS="$(DCFLAGS)"

cgdb: 
	make CFLAGS="$(DCFLAGS)"
	cgdb exar

clean: 
	$(RM) -rf $(OBJ) $(TARGET)

.PHONY: clean
