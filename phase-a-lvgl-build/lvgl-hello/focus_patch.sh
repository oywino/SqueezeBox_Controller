#!/bin/sh
set -eu
cd /workspace/phase-a-lvgl-build/lvgl-hello

# Add visible focus style after `lv_init();`
perl -0777 -i -pe 's/lv_init\(\);\n/lv_init();\n\n  static lv_style_t style_focused;\n  lv_style_init(&style_focused);\n  lv_style_set_outline_width(&style_focused, 4);\n  lv_style_set_outline_color(&style_focused, lv_palette_main(LV_PALETTE_YELLOW));\n  lv_style_set_outline_pad(&style_focused, 2);\n  lv_style_set_bg_opa(&style_focused, LV_OPA_COVER);\n  lv_style_set_bg_color(&style_focused, lv_palette_main(LV_PALETTE_BLUE));\n/' main.c

# Apply the focused style to both buttons after they are created
perl -0777 -i -pe 's/lv_obj_t \*btn1 = lv_btn_create\(scr\);\n/lv_obj_t *btn1 = lv_btn_create(scr);\n  lv_obj_add_style(btn1, &style_focused, LV_STATE_FOCUSED);\n/' main.c
perl -0777 -i -pe 's/lv_obj_t \*btn2 = lv_btn_create\(scr\);\n/lv_obj_t *btn2 = lv_btn_create(scr);\n  lv_obj_add_style(btn2, &style_focused, LV_STATE_FOCUSED);\n/' main.c

sh -x ./build.sh > build.log 2>&1 || true
tail -n 10 build.log
ls -lh lvgl-hello-armv5
