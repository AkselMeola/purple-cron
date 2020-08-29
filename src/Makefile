#!/usr/bin/make -f

VERSION = $(shell cat ../VERSION)

TARGET := purplecron.so

#############################
# Local paths 				#
#############################

INCLUDES =../include
LOCAL_LIBS =../lib
OBJECTS=obj

#############################
# Shared recources 			#
#############################

SHARED_CFLAGS = \
	-I$(INCLUDES) \
	$(shell pkg-config --cflags glib-2.0)	\
	$(shell pkg-config --cflags libcurl) \
	$(shell pkg-config --cflags purple)

SHARED_LIBS = \
	$(shell pkg-config --libs glib-2.0)	\
	$(shell pkg-config --libs libcurl) \
	$(shell pkg-config --libs purple)


#############################
# Compiler flags 			#
#############################

CC=gcc

CFLAGS= \
	$(SHARED_CFLAGS) \
	-g \
	-g3 \
	-Wall \
	-fPIC \
	-Wextra \
	-Wformat \
	-std=c99 \
	-Wno-unused-parameter \
	-Werror=format-security

LDFLAGS ?= \
	-shared \
	-fPIC

LIBS= \
	$(SHARED_LIBS) \
	-ljansson

# Find all .h files in includes directory
_DEPS = $(wildcard *.h)
DEPS = $(patsubst %, $(INCLUDES)/%, $(_DEPS))

# Find all .c files and rename them as .o files
_OBJ = $(patsubst %.c, %.o, $(wildcard *.c))
OBJ = $(patsubst %,$(OBJECTS)/%,$(_OBJ))

.PHONY: clean all install

$(OBJECTS)/%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(CFLAGS) $(LIBS)

# Clean up
clean:
	rm -f $(TARGET) $(OBJECTS)/*.o *~ core $(INCDIR)/*~

install:
	cp purplecron.so $(HOME)/.purple/plugins/

try:
	-purple-remote quit
	cp purplecron.so $(HOME)/.purple/plugins/
	cp ../scripts/purplecron $(HOME)/.purple/
	mkdir -p $(HOME)/.purple/purplecron.d
	pidgin &
