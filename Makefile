# rotation_logger make file
#

.PHONY: all install clean uninstall

all : rotation_logger  Makefile

install : /usr/local/bin/rotation_logger  Makefile

/usr/local/bin/rotation_logger : rotation_logger  Makefile
	sudo cp -f rotation_logger /usr/local/bin/rotation_logger

rotation_logger : rotation_logger.c  Makefile
	gcc -Wall -pipe -o rotation_logger  rotation_logger.c

clean:
	rm -f *.o *~

uninstall:
	rm -f rotation_logger

# end
