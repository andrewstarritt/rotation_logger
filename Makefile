# rotation_logger make file
#

.PHONY: all install clean uninstall

all : rotation_logger  Makefile

install : rotation_logger  Makefile

rotation_logger : rotation_logger.c  Makefile
	gcc -Wall -pipe -o rotation_logger  rotation_logger.c

clean:
	rm -f rotation_logger

uninstall:
	@:

# end
