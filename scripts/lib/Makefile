# See COPYING for copyright and license details
all: $(patsubst %.js.in, %.js, $(wildcard *.js.in)) 

%.js: %.js.in minify
	@echo gen $@ 
	@./minify $< $@

minify: minify.c
	$(CC) $< -o $@

clean:  
	$(RM) $(patsubst %.js.in, %.js, $(wildcard *.js.in)) 
	$(RM) minify

.PHONY: clean
