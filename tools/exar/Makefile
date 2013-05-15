CFLAGS += -Wall -pedantic -Werror -std=c99 -g 

TARGET=exar
OBJ=$(patsubst %.c, %.o, $(wildcard *.c))

all: $(TARGET)

$(TARGET): $(OBJ)
	@echo $(CC) $@
	@$(CC) $(OBJ) -o $@ $(CFLAGS) $(CPPFLAGS)

%.o: %.c 
	@echo $(CC) $<
	@$(CC) -c $< -o $@ $(CFLAGS) $(CPPFLAGS)

clean: 
	$(RM) -rf $(OBJ) $(TARGET)

.PHONY: clean
