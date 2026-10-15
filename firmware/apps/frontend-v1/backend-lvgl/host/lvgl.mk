# lvgl.mk — builds LVGL v9.2 (cloned into build/lvgl) as a host static library.
# Invoked by ../../Makefile after the clone exists. Spare original.
LVGL ?= ../../../../../build/lvgl
OBJ  ?= ../../../../../build/frontend-preview/obj
CONF ?= .
CC   := xcrun clang
CFLAGS := -O2 -std=c11 -w -DLV_CONF_INCLUDE_SIMPLE -I$(CONF) -I$(LVGL)
SRCS := $(shell find $(LVGL)/src -name '*.c')
OBJS := $(patsubst $(LVGL)/src/%.c,$(OBJ)/lvgl/%.o,$(SRCS))

$(OBJ)/liblvgl.a: $(OBJS)
	@rm -f $@; xcrun ar rcs $@ $(OBJS)
	@echo "  AR    $@ ($(words $(OBJS)) objs)"

$(OBJ)/lvgl/%.o: $(LVGL)/src/%.c $(CONF)/lv_conf.h
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@
