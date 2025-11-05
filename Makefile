SRC_DIR := src
BUILD_DIR := bin
OBJS := $(SRC_DIR)/main.o \
		$(SRC_DIR)/image_util.o \
		$(SRC_DIR)/qmage_tables.o \
		$(SRC_DIR)/im_decode.o \
		$(SRC_DIR)/im_encode.o
LIBS := -lm -lz -lpng
CC := gcc
CFLAGS := -O2
LD := gcc
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
