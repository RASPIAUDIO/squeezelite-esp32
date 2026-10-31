# libalac make

CFLAGS = -g -O3 -c
LFLAGS = -Wall
CC = g++

SRCDIR = .
OBJDIR = ./obj
INCLUDES = .

HEADERS = \
$(SRCDIR)/EndianPortable.h \
$(SRCDIR)/aglib.h \
$(SRCDIR)/ALACAudioTypes.h \
$(SRCDIR)/ALACBitUtilities.h\
$(SRCDIR)/ALACDecoder.h \
$(SRCDIR)/ALACEncoder.h \
$(SRCDIR)/dplib.h \
$(SRCDIR)/matrixlib.h

SOURCES = \
$(SRCDIR)/EndianPortable.c \
$(SRCDIR)/ALACBitUtilities.c \
$(SRCDIR)/ALACDecoder.cpp \
$(SRCDIR)/ALACEncoder.cpp \
$(SRCDIR)/ag_dec.c \
$(SRCDIR)/ag_enc.c \
$(SRCDIR)/dp_dec.c \
$(SRCDIR)/dp_enc.c \
$(SRCDIR)/matrix_dec.c \
$(SRCDIR)/matrix_enc.c

OBJS = $(patsubst %.o,$(OBJDIR)/%.o,$(addsuffix .o,$(basename $(SOURCES))))

libalac.a: directory $(OBJS)
	$(AR) rcs $(OBJDIR)/libalac.a $(OBJS)

directory:
	mkdir -p $(OBJDIR)
	
$(OBJDIR)/%.o : %.c
	$(CC) -I $(INCLUDES) $(CFLAGS) $< -c -o $@
	
$(OBJDIR)/%.o : %.cpp
	$(CC) -I $(INCLUDES) $(CFLAGS) $< -c -o $@
	
clean:
	-rm $(OBJS) $(OBJDIR)/libalac.a
