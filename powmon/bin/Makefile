
CC = gcc
CFLAGS = -std=gnu89
INSTALL = install
PREFIX=./bin
INC = -I../lib/iniparser/src -I../lib/mosquitto-1.3.5/lib  
LDIR = -L/usr/lib -L../lib/iniparser
LIBMOSQ = ../lib/mosquitto-1.3.5/lib/libmosquitto.a
LIBS = -lm $(LIBMOSQ)   -lssl -lcrypto -lrt -lpthread -liniparser
FILES = pow_mon.c sensor_read_lib.c 
TARGET = pow_mon
DESTDIR=$(PREFIX)
CFGFILE=pow_mon.conf 


build: $(FILES)
	
	$(CC) $(CFLAGS) -o $(TARGET) $(FILES) $(INC) $(LDIR) $(LIBS)
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
