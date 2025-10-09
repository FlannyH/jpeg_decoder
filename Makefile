NAME = jpeg_dec
SOURCE_DIR = .
BUILD_DIR = build
OBJ_DIR = obj

EXE_EXT =
ifeq ($(OS),Windows_NT)
	EXE_EXT = .exe
endif
LIB_EXT = .so
ifeq ($(OS),Windows_NT)
	LIB_EXT = .dll
endif

all: standalone miv

# STANDALONE
standalone: $(OBJ_DIR) $(BUILD_DIR)/$(NAME)$(EXE_EXT) $(OBJ_DIR)/$(NAME)_standalone.s

$(OBJ_DIR)/$(NAME)_standalone.s: $(BUILD_DIR)/$(NAME)$(EXE_EXT)
	objdump -d $< -l > $@

$(BUILD_DIR)/$(NAME)$(EXE_EXT): $(SOURCE_DIR)/jpeg_dec.c $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@ -lm -DSTANDALONE=1 -DMIV_LIBRARY=0

# MIV
miv: $(OBJ_DIR) $(BUILD_DIR)/$(NAME)$(LIB_EXT) $(OBJ_DIR)/$(NAME)_miv.s

$(OBJ_DIR)/$(NAME)_miv.s: $(BUILD_DIR)/$(NAME)$(LIB_EXT)
	objdump -d $< -l > $@

$(BUILD_DIR)/$(NAME)$(LIB_EXT): $(SOURCE_DIR)/jpeg_dec.c $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@ -lm -fPIC -shared -DSTANDALONE=0 -DMIV_LIBRARY=1

CC = gcc
# CFLAGS = -Wall -Wextra -std=c11 -g3 -O0
CFLAGS = -Wall -Wextra -std=c11 -O3 -ftree-vectorize -msse2 -ffast-math

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
	rm -rf $(OBJ_DIR)
