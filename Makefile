# -----------------------------
# Compiler & flags
# -----------------------------
CC := clang
AR := ar
ARFLAGS := rcs
CFLAGS := -Wall -Wextra -Werror -pedantic -std=c99 -g -fsanitize=address -MMD -MP -Iinclude -Isrc -D_XOPEN_SOURCE=700

# -----------------------------
# Directories
# -----------------------------
SRC_DIR := src
BUILD_DIR := build
BIN_DIR := bin
LIB_DIR := lib
TARGET := $(BIN_DIR)/excalibur

# -----------------------------
# Modules
# -----------------------------
# Library modules: directories starting with lib*
LIB_SRC_DIRS := $(shell find $(SRC_DIR) -mindepth 1 -maxdepth 1 -type d -name 'lib*')
LIB_NAMES := $(notdir $(LIB_SRC_DIRS))

# App modules: directories not starting with lib*
APP_SRC_DIRS := $(shell find $(SRC_DIR) -mindepth 1 -maxdepth 1 -type d ! -name 'lib*')
APP_NAMES := $(notdir $(APP_SRC_DIRS))

# -----------------------------
# Unity source files (one per module)
# -----------------------------
LIB_ENTRY_SRCS := $(foreach lib,$(LIB_NAMES),$(SRC_DIR)/$(lib)/$(lib).c)
APP_ENTRY_SRCS := $(foreach app,$(APP_NAMES),$(SRC_DIR)/$(app)/$(app).c)

# Check that entry files exist
$(foreach f,$(LIB_ENTRY_SRCS) $(APP_ENTRY_SRCS),\
  $(if $(wildcard $(f)),,$(error Missing required module entry file: $(f)))\
)

# -----------------------------
# Object files
# -----------------------------
LIB_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(LIB_ENTRY_SRCS))
APP_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(APP_ENTRY_SRCS))

# -----------------------------
# Library files (Internal + External)
# -----------------------------
INTERNAL_LIBS := $(patsubst %,$(LIB_DIR)/%.a,$(LIB_NAMES))

EXTERNAL_NAMES := $(shell awk '!/^[[:space:]]*#/ && NF { \
  sub(/^override[[:space:]]+/, ""); \
  gsub(/@.*$/, ""); \
  n = split($$1, parts, "/"); \
  print parts[n]; \
}' excalibur.txt 2>/dev/null)
EXTERNAL_LIBS := $(patsubst %,$(LIB_DIR)/%.a,$(EXTERNAL_NAMES))

STATIC_LIB_FILES := $(INTERNAL_LIBS) $(EXTERNAL_LIBS)

# -----------------------------
# Dependencies
# -----------------------------
DEPS := $(LIB_OBJS:.o=.d) $(APP_OBJS:.o=.d)

# -----------------------------
# Compile rule for unity files
# -----------------------------
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# -----------------------------
# Build static library for each internal lib module
# -----------------------------
define MAKE_STATIC_LIB
$(LIB_DIR)/$1.a: $(BUILD_DIR)/$1/$1.o
	@mkdir -p $(LIB_DIR)
	$(AR) $(ARFLAGS) $$@ $$^
endef

$(foreach lib,$(LIB_NAMES),$(eval $(call MAKE_STATIC_LIB,$(lib))))

# -----------------------------
# Dependency Resolution
# -----------------------------
$(LIB_DIR)/.excal.stamp: excalibur.txt $(TARGET)
	@echo "Checking dependencies..."
	./bin/excalibur install
	@mkdir -p $(LIB_DIR)
	@touch $(LIB_DIR)/.excal.stamp

# -----------------------------
# Main program
# -----------------------------
MAIN_OBJ := $(BUILD_DIR)/main.o

# -----------------------------
# Final executable
# -----------------------------
# Ensure the final executable waits for dependency resolution FIRST
$(TARGET): $(LIB_DIR)/.excal.stamp $(MAIN_OBJ) $(APP_OBJS) $(STATIC_LIB_FILES)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(MAIN_OBJ) $(APP_OBJS) $(STATIC_LIB_FILES) -o $@

all: $(TARGET)

run: $(TARGET)
	$(TARGET)

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
	rm -f $(INTERNAL_LIBS)
	rm -f $(LIB_DIR)/.excal.stamp

-include $(DEPS)

.PHONY: all clean run
