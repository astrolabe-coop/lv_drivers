# Wayland display and input driver

Wayland display and input driver, with support for keyboard, mouse and touchscreen.
Keyboard support is based on libxkbcommon.

Following shell are supported:

* wl_shell (deprecated)
* xdg_shell
* IVI shell (ivi-application protocol)

> xdg_shell and IVI shell require an extra build step; see section _Generate protocols_ below.


Basic client-side window decorations (simple title bar, minimize and close buttons)
are supported, while integration with desktop environments is not.


## Install headers and libraries

### Ubuntu

```
sudo apt-get install libwayland-dev libxkbcommon-dev libwayland-bin wayland-protocols
```

### Fedora

```
sudo dnf install wayland-devel libxkbcommon-devel wayland-utils wayland-protocols-devel
```


## Generate protocols

Support for non-basic shells (i.e. other than _wl_shell_) requires additional
source files to be generated before the first build of the project. To do so,
navigate to the _wayland_ folder (the one which includes this file) and issue
the following commands:

```
cmake .
make
```


## Build configuration under Eclipse

In "Project properties > C/C++ Build > Settings" set the followings:

- "Cross GCC Compiler > Command line pattern"
  - Add ` ${wayland-cflags}` and ` ${xkbcommon-cflags}` to the end (add a space between the last command and this)


- "Cross GCC Linker > Command line pattern"
  - Add ` ${wayland-libs}` and ` ${xkbcommon-libs}`  to the end (add a space between the last command and this)


- In "C/C++ Build > Build variables"
  - Configuration: [All Configuration]

  - Add
    - Variable name: `wayland-cflags`
      - Type: `String`
      - Value: `pkg-config --cflags wayland-client`
    - Variable name: `wayland-libs`
      - Type: `String`
      - Value: `pkg-config --libs wayland-client`
    - Variable name: `xkbcommon-cflags`
      - Type: `String`
      - Value: `pkg-config --cflags xkbcommon`
    - Variable name: `xkbcommon-libs`
      - Type: `String`
      - Value: `pkg-config --libs xkbcommon`


## Init Wayland in LVGL

1. In `main.c` `#incude "lv_drivers/wayland/wayland.h"`
2. Enable the Wayland driver in `lv_drv_conf.h` with `USE_WAYLAND 1` and
   configure its features below, enabling at least support for one shell.
3. `LV_COLOR_DEPTH` should be set either to `32` or `16` in `lv_conf.h`;
   support for `8` and `1` depends on target platform.
4. After `lv_init()` call `lv_wayland_init()`
5. After `lv_deinit()` call `lv_wayland_deinit()`
6. Add a display:
```c
  static lv_disp_draw_buf_t draw_buf;
  static lv_color_t buf1[WAYLAND_HOR_RES * WAYLAND_VER_RES];
  lv_disp_draw_buf_init(&draw_buf, buf1, NULL, WAYLAND_HOR_RES * WAYLAND_VER_RES);

  /* Create a display */
  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.draw_buf = &draw_buf;
  disp_drv.hor_res = WAYLAND_HOR_RES;
  disp_drv.ver_res = WAYLAND_VER_RES;
  disp_drv.flush_cb = lv_wayland_flush;
  lv_disp_t * disp = lv_disp_drv_register(&disp_drv);
```
7. Add keyboard:
```c
  lv_indev_drv_t indev_drv_kb;
  lv_indev_drv_init(&indev_drv_kb);
  indev_drv_kb.type = LV_INDEV_TYPE_KEYPAD;
  indev_drv_kb.read_cb = lv_wayland_keyboard_read;
  lv_indev_drv_register(&indev_drv_kb);
```
8. Add touchscreen:
```c
  lv_indev_drv_t indev_drv_touch;
  lv_indev_drv_init(&indev_drv_touch);
  indev_drv_touch.type = LV_INDEV_TYPE_POINTER;
  indev_drv_touch.read_cb = lv_wayland_touch_read;
  lv_indev_drv_register(&indev_drv_touch);
```
9. Add mouse:
```c
  lv_indev_drv_t indev_drv_mouse;
  lv_indev_drv_init(&indev_drv_mouse);
  indev_drv_mouse.type = LV_INDEV_TYPE_POINTER;
  indev_drv_mouse.read_cb = lv_wayland_pointer_read;
  lv_indev_drv_register(&indev_drv_mouse);
```
10. Add mouse wheel as encoder:
```c
  lv_indev_drv_t indev_drv_mousewheel;
  lv_indev_drv_init(&indev_drv_mousewheel);
  indev_drv_mousewheel.type = LV_INDEV_TYPE_ENCODER;
  indev_drv_mousewheel.read_cb = lv_wayland_pointeraxis_read;
  lv_indev_drv_register(&indev_drv_mousewheel);
```
