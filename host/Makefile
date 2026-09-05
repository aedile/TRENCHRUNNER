CC ?= cc
CFLAGS ?= -O2 -Wall -Wno-unused-function -Wno-duplicate-decl-specifier -DSW_DEBUG -I../core
SRCS = harness.c ../core/starwars.c ../core/avg.c ../core/sound.c ../core/pokey.c ../core/tms5220.c
harness: $(SRCS) ../core/*.h ../core/e6809.c
	$(CC) $(CFLAGS) -o $@ $(SRCS)
clean:
	rm -f harness
