
CC = gcc
CFLAGS = -std=gnu89
INSTALL = install
PREFIX=./bin
LDIR = -L/usr/lib 
LIBS = -lm -lrt -lpthread 
FILES = pow_mon.c sensor_read_lib.c 
TARGET = energy_app
DESTDIR=$(PREFIX)


build: $(FILES)
	
	$(CC) $(CFLAGS) -o $(TARGET) $(FILES)  $(LDIR) $(LIBS)
	#sudo setcap cap_sys_rawio=ep $(TARGET)

install:
	@echo building: $(TARGET)
	-mkdir -p $(DESTDIR)
	$(INSTALL) -m 744 $(TARGET) $(DESTDIR)
	$(INSTALL) -m 644 $(CFGFILE) $(DESTDIR)
	
clean:
	rm -f $(TARGET)
	rm -f ./bin/$(TARGET)

rebuild: clean build
