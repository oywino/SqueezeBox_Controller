#!/bin/sh
set -eu
cd /workspace/phase-a-lvgl-build/lvgl-hello

# Reset to the known encoder+buttons base (deterministic)
sh ./update_and_build.sh >/dev/null 2>&1 || true

# 1) Two framebuffer pages
perl -0777 -i -pe 's/static uint8_t \*g_fb = NULL;/static uint8_t *g_fb0 = NULL;\nstatic uint8_t *g_fb1 = NULL;/' main.c
perl -0777 -i -pe 's/g_fb = fb \+ \(size_t\)g_line_len \* \(size_t\)vinfo\.yoffset;/g_fb0 = fb + (size_t)g_line_len * 0;\n\n  g_fb1 = fb + (size_t)g_line_len * (size_t)g_h;/' main.c
perl -0777 -i -pe 's/uint8_t \*dst = g_fb \+ \(size_t\)y \* \(size_t\)g_line_len \+ \(size_t\)x1 \* 2;\n    memcpy\(dst, src, \(size_t\)copy_w \* 2\);/uint8_t *dst0 = g_fb0 + (size_t)y * (size_t)g_line_len + (size_t)x1 * 2;\n    uint8_t *dst1 = g_fb1 + (size_t)y * (size_t)g_line_len + (size_t)x1 * 2;\n    memcpy(dst0, src, (size_t)copy_w * 2);\n    memcpy(dst1, src, (size_t)copy_w * 2);/g' main.c

# 2) Grab inputs so jive won't receive wheel/buttons
perl -0777 -i -pe 's/printf\("input: wheel_fd=%d keys_fd=%d\\n", g_fd_wheel, g_fd_keys\);\n/printf("input: wheel_fd=%d keys_fd=%d\\n", g_fd_wheel, g_fd_keys);\n  if(g_fd_wheel >= 0) ioctl(g_fd_wheel, EVIOCGRAB, 1);\n  if(g_fd_keys  >= 0) ioctl(g_fd_keys,  EVIOCGRAB, 1);\n/' main.c

# 3) Periodic redraw
perl -0777 -i -pe 's/int main\(void\) \{/static void keep_visible(lv_timer_t *t) {\n  (void)t;\n  lv_obj_invalidate(lv_scr_act());\n}\n\nint main(void) {/' main.c
perl -0777 -i -pe 's/lv_group_focus_obj\(btn1\);\n/lv_group_focus_obj(btn1);\n\n  lv_timer_create(keep_visible, 30, NULL);\n/' main.c

sh -x ./build.sh > build.log 2>&1 || true
tail -n 20 build.log
ls -lh lvgl-hello-armv5
