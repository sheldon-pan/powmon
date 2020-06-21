
CC = gcc
CFLAGS = -std=gnu89
INSTALL = install
PREFIX=./bin
LIBS = -lm $(LIBMOSQ)  -lrt -lpthread 
FILES = pow_mon.c sensor_read_lib.c 
TARGET = pow_node_smp
DESTDIR=$(PREFIX)



build: $(FILES)
	
	$(CC) $(CFLAGS) -o $(TARGET) $(FILES) $(INC) $(LDIR) $(LIBS)
	#sudo setcap cap_sys_rawio=ep $(TARGET)

install:
	@echo building: $(TARGET)
	-mkdir -p $(DESTDIR)
	$(INSTALL) -m 744 $(TARGET) $(DESTDIR)
	
clean:
	rm -f $(TARGET)
	rm -f ./bin/$(TARGET)

rebuild: clean build
