SRC_DIR := src
BUILD_DIR := bin

OBJS := $(SRC_DIR)/main.o \
		$(SRC_DIR)/image_util.o \
		$(SRC_DIR)/qmage_tables.o \
		$(SRC_DIR)/im_decode.o \
		$(SRC_DIR)/im_encode.o
LIBS := -lm -lz -lpng

ifeq ($(OS),Windows_NT)
	CC := x86_64-w64-mingw32-gcc
	LD := x86_64-w64-mingw32-gcc
else
	CC := gcc
	LD := gcc
endif

CFLAGS := -O2 -g
LDFLAGS := $(LIBS)

all: imtool

imtool: $(OBJS)
	mkdir -p $(BUILD_DIR)
	$(LD) -o $(BUILD_DIR)/$@ $^ $(LDFLAGS)

%.o: %.c %.h
	$(CC) -c -o $@ $< $(CFLAGS)

install:
	install $(BUILD_DIR)/imtool /usr/local/bin

clean:
	rm -f $(OBJS)
