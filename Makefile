NAME = jpeg_dec
SOURCE_DIR = .
BUILD_DIR = build

EXE_EXT =
ifeq ($(OS),Windows_NT)
	EXE_EXT = .exe
endif

all: $(BUILD_DIR)/$(NAME)$(EXE_EXT)

$(BUILD_DIR)/$(NAME)$(EXE_EXT): $(SOURCE_DIR)/jpeg_dec.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@
	objcopy --only-keep-debug $(NAME)$(EXE_EXT) $(NAME).debug

CC = gcc
# CFLAGS = -Wall -Wextra -std=c11 -g3 -O0
# CFLAGS = -Wall -Wextra -std=c11 -O0
# CFLAGS = -Wall -Wextra -std=c11 -O1
# CFLAGS = -Wall -Wextra -std=c11 -O2
# CFLAGS = -Wall -Wextra -std=c11 -O3
# CFLAGS = -Wall -Wextra -std=c11 -O3 -ftree-vectorize -march=native
# CFLAGS = -Wall -Wextra -std=c11 -O3 -ftree-vectorize -march=native -fopt-info-vec-all
CFLAGS = -Wall -Wextra -std=c11 -O3 -ftree-vectorize -march=native -fopt-info-vec-all -ffast-math -flto

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
