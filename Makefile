CC=gcc
CFLAGS=-Iopenonload-201502-u2/src/driver/linux_net -Wall 

sfphytune: sfphytune.c
	$(CC) -o $@ $< $(CFLAGS)

clean:
	rm -f sfphytune
