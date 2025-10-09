NAME = jpeg_dec
SOURCE_DIR = .
BUILD_DIR = build
OBJ_DIR = .

EXE_EXT =
ifeq ($(OS),Windows_NT)
	EXE_EXT = .exe
endif

all: $(BUILD_DIR)/$(NAME)$(EXE_EXT) $(OBJ_DIR)/$(NAME).s

$(BUILD_DIR)/$(NAME)$(EXE_EXT): $(SOURCE_DIR)/jpeg_dec.c $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@ -lm

$(OBJ_DIR)/$(NAME).s: $(BUILD_DIR)/$(NAME)$(EXE_EXT)
	objdump -d $< -l > $@

CC = gcc
# CFLAGS = -Wall -Wextra -std=c11 -g3 -O0
# CFLAGS = -Wall -Wextra -std=c11 -O0
# CFLAGS = -Wall -Wextra -std=c11 -O1
# CFLAGS = -Wall -Wextra -std=c11 -O2
# CFLAGS = -Wall -Wextra -std=c11 -O3
# CFLAGS = -Wall -Wextra -std=c11 -O3 -ftree-vectorize -march=native
# CFLAGS = -Wall -Wextra -std=c11 -O3 -ftree-vectorize -march=native -fopt-info-vec-all
CFLAGS = -Wall -Wextra -std=c11 -O3 -ftree-vectorize -march=native -ffast-math

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
	rm -rf $(OBJ_DIR)
