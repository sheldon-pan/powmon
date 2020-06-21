CFLAGS=-I. -O3 -g -fPIC -I./include
Q_AR = ar
CC= mpicc
LIBDIR=lib
OBJDIR=bin

all: $(LIBDIR)/libpowmpi.a $(LIBDIR)/libpowmpi.so

OBJ = $(OBJDIR)/powmpi.o  $(OBJDIR)/sensor_read_lib.o


$(OBJ): $(OBJDIR)/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@ $(LIB)

$(LIBDIR)/libpowmpi.a: $(OBJ)
	$(Q_AR) rcs $@ $(OBJ)

$(LIBDIR)/libpowmpi.so: $(OBJ)
	`which mpicc` -shared -o $@ $(OBJ)

clean:
	rm -f lib/*.a bin/*.o lib/*.so a.out
