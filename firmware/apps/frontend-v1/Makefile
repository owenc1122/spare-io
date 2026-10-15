# frontend-v1 — host preview and unit tests.
#   make preview       build LVGL v9.2 for macOS, render every page/state + transitions
#                      through fe_state + fe_pages + backend-lvgl -> build/frontend-preview/
#   make test          host unit tests of the pure state machine / registry
# Everything generated lives under build/ (gitignored). Spare original.
REPO     := $(abspath ../../..)
BUILD    := $(REPO)/build
LVGL     := $(BUILD)/lvgl
LVGL_TAG := v9.2.2
OUT      := $(BUILD)/frontend-preview$(if $(filter en,$(SG_LANG)),-en,)
OBJ      := $(OUT)/obj
BK       := backend-lvgl
CC       := xcrun clang
# Open CJK font (OFL): Noto Sans SC static TTFs, Regular + Bold (tiny_ttf needs
# glyf outlines, not CFF). Override NOTO_SC_DIR with any folder holding both.
NOTO_SC_DIR ?= $(REPO)/fonts
FONT_R   := $(BUILD)/fonts/NotoSansSC-Regular.ttf
FONT_B   := $(BUILD)/fonts/NotoSansSC-Bold.ttf
# UI language: zh (default, matches the device) or en. `make preview SG_LANG=en`
# renders the English string set; any glyf-outline TTF pair works for Latin
# text, e.g. FONT_R=/path/Inter-Regular.ttf FONT_B=/path/Inter-Bold.ttf.
SG_LANG  ?= zh
LANG_DEF := $(if $(filter en,$(SG_LANG)),-DSG_LANG_EN,)
ICON_BOX := $(shell sed -n 's/^\#define FE_ICON_BOX[ \t]*\([0-9]*\).*/\1/p' fe_pages.h)

HOST_CFLAGS := -O1 -g -std=c11 -Wall -Wextra -Wno-comment -DLV_CONF_INCLUDE_SIMPLE $(LANG_DEF) \
               -I$(BK)/host -I$(LVGL) -I. -I$(OUT)
PAGES_SRC   := fe_state.c fe_registry.c fe_runtime.c fe_pages.c $(BK)/fp_display_lvgl.c $(BK)/host/preview_main.c

.PHONY: preview test lvgl clean-preview
preview: $(OUT)/preview
	@rm -rf $(OUT)/raw && mkdir -p $(OUT)/raw
	$(OUT)/preview "$(FONT_R)" "$(FONT_B)" "$(OUT)/raw"
	python3 $(BK)/host/assemble.py "$(OUT)"
	@echo "preview -> $(OUT)"

$(LVGL)/lvgl.h:
	git -c advice.detachedHead=false clone -q --depth 1 -b $(LVGL_TAG) https://github.com/lvgl/lvgl $(LVGL)

lvgl: $(LVGL)/lvgl.h
	@$(MAKE) --no-print-directory -j8 -f $(BK)/host/lvgl.mk LVGL=$(LVGL) OBJ=$(OBJ) CONF=$(BK)/host

$(BUILD)/fonts/NotoSansSC-%.ttf:
	@mkdir -p $(dir $@)
	@test -f "$(NOTO_SC_DIR)/$(notdir $@)" || { echo "need $(notdir $@): set NOTO_SC_DIR=/folder/with/NotoSansSC-{Regular,Bold}.ttf"; exit 1; }
	cp "$(NOTO_SC_DIR)/$(notdir $@)" "$@"

$(OUT)/host_icons.h: $(BK)/host/gen_host_icons.py fe_pages.h $(wildcard assets/icons/*.svg)
	python3 $(BK)/host/gen_host_icons.py --box $(ICON_BOX) --out $@

$(OUT)/preview: lvgl $(FONT_R) $(FONT_B) $(OUT)/host_icons.h $(PAGES_SRC) $(wildcard *.h assets/*/*.h) $(BK)/fp_display_lvgl.h
	$(CC) $(HOST_CFLAGS) $(PAGES_SRC) $(OBJ)/liblvgl.a -lm -o $@

test:
	@mkdir -p $(OBJ)
	$(CC) -std=c11 -Wall -Wextra -Wno-comment -fsanitize=address,undefined -I . tests/test_fe_state.c fe_state.c -o $(OBJ)/t_state && $(OBJ)/t_state
	$(CC) -std=c11 -Wall -Wextra -Wno-comment -fsanitize=address,undefined -I . tests/test_fe_registry.c fe_registry.c -o $(OBJ)/t_reg && $(OBJ)/t_reg
	$(CC) -std=c11 -Wall -Wextra -Wno-comment -fsanitize=address,undefined -I . tests/test_fe_pages.c fe_pages.c fe_state.c -o $(OBJ)/t_pages && $(OBJ)/t_pages
	$(CC) -std=c11 -Wall -Wextra -Wno-comment -fsanitize=address,undefined -I . tests/test_fe_runtime.c fe_runtime.c fe_pages.c fe_state.c fe_registry.c -o $(OBJ)/t_rt && $(OBJ)/t_rt

clean-preview:
	rm -rf $(OUT)
