/**
 * @file wayland.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "wayland.h"

#if USE_WAYLAND

#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/mman.h>

#include <linux/input.h>

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

#if LV_WAYLAND_XDG_SHELL
#include "protocols/wayland-xdg-shell-client-protocol.h"
#endif

#if LV_WAYLAND_IVI_APPLICATION
#include "protocols/wayland-ivi-application-client-protocol.h"
#endif

/*********************
 *      DEFINES
 *********************/

#define BYTES_PER_PIXEL ((LV_COLOR_DEPTH + 7) / 8)

#if LV_WAYLAND_CLIENT_SIDE_DECORATIONS
#define TITLE_BAR_HEIGHT 24
#define BUTTON_MARGIN LV_MAX((TITLE_BAR_HEIGHT / 6), 1)
#define BUTTON_PADDING LV_MAX((TITLE_BAR_HEIGHT / 8), 1)
#define BUTTON_SIZE (TITLE_BAR_HEIGHT - (2 * BUTTON_MARGIN))
#endif

/**********************
 *      MACROS
 **********************/

#define input_to_parent(_input, _parent) \
    (_parent *)((char *)_input - offsetof(_parent, input))

/**********************
 *      TYPEDEFS
 **********************/

enum parent_type
{
    PARENT_IS_WINDOW,
#if LV_WAYLAND_CLIENT_SIDE_DECORATIONS
    PARENT_IS_DECORATION,
    PARENT_IS_BUTTON
#endif
};

struct window;
struct input
{
    struct
    {
        lv_coord_t x;
        lv_coord_t y;
        lv_indev_state_t left_button;
        lv_indev_state_t right_button;
        lv_indev_state_t wheel_button;
        int16_t wheel_diff;
    } mouse;

    struct
    {
        lv_key_t key;
        lv_indev_state_t state;
    } keyboard;

    struct
    {
        lv_coord_t x;
        lv_coord_t y;
        lv_indev_state_t state;
    } touch;

    struct window *window;
    enum parent_type parent_type;
};

struct seat
{
    struct wl_touch *wl_touch;
    struct wl_pointer *wl_pointer;
    struct wl_keyboard *wl_keyboard;

    struct
    {
        struct xkb_keymap *keymap;
        struct xkb_state *state;
    } xkb;
};

struct application
{
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_subcompositor *subcompositor;
    struct wl_shm *shm;
    struct wl_seat *wl_seat;

    struct wl_cursor_theme *cursor_theme;
    struct wl_surface *cursor_surface;

#if LV_WAYLAND_WL_SHELL
    struct wl_shell *wl_shell;
#endif

#if LV_WAYLAND_XDG_SHELL
    struct xdg_wm_base *xdg_wm;
#endif

#if LV_WAYLAND_IVI_APPLICATION
    struct ivi_application *ivi_application;
    uint32_t ivi_id_base;
#endif

    const char *xdg_runtime_dir;

#ifdef LV_WAYLAND_CLIENT_SIDE_DECORATIONS
    bool opt_disable_decorations;
#endif

    uint32_t format;

    struct xkb_context *xkb_context;

    struct seat seat;

    struct input *touch;
    struct input *pointer;
    struct input *keyboard;

    lv_ll_t window_ll;

    bool cursor_flush_pending;
};

#if LV_WAYLAND_CLIENT_SIDE_DECORATIONS
enum button_type {
    BUTTON_TYPE_CLOSE,
    BUTTON_TYPE_MINIMIZE,
    NUM_BUTTONS
};

struct button
{
    enum button_type type;

    struct wl_buffer *buffer;
    struct wl_surface *surface;
    struct wl_subsurface *subsurface;

    void *data;

    struct input input;
};

struct decoration
{
    struct wl_buffer *buffer;
    struct wl_surface *surface;
    struct wl_subsurface *subsurface;

    void *data;

    struct input input;
};
#endif

struct window
{
    struct application *application;
    struct wl_shm_pool *shm_pool;

    struct wl_buffer *buffer;
    struct wl_surface *surface;

#if LV_WAYLAND_WL_SHELL
    struct wl_shell_surface *wl_shell_surface;
#endif

#if LV_WAYLAND_XDG_SHELL
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
#endif

#if LV_WAYLAND_IVI_APPLICATION
    struct ivi_surface *ivi_surface;
#endif

#if LV_WAYLAND_CLIENT_SIDE_DECORATIONS
    struct decoration * decoration[1];
    struct button * button[NUM_BUTTONS];
#endif

    int width;
    int height;
    void *data;
    unsigned int data_size;
    unsigned int data_offset;

    struct input input;

    bool flush_pending;
    bool cycled;
    bool shall_close;
    bool closed;

    void (*ext_monitor_cb)(lv_disp_drv_t * disp_drv, uint32_t time, uint32_t px);
};

/*********************************
 *   STATIC VARIABLES and FUNTIONS
 *********************************/

static struct application application;

static inline bool _is_digit(char ch)
{
    return (ch >= '0') && (ch <= '9');
}

static unsigned int _atoi(const char ** str)
{
    unsigned int i = 0U;
    while (_is_digit(**str))
    {
        i = i * 10U + (unsigned int)(*((*str)++) - '0');
    }
    return i;
}

static void shm_format(void *data, struct wl_shm *wl_shm, uint32_t format)
{
    struct application *app = data;

    switch (format)
    {
#if (LV_COLOR_DEPTH == 32)
    case WL_SHM_FORMAT_ARGB8888:
        app->format = format;
        break;
    case WL_SHM_FORMAT_XRGB8888:
        if (app->format != WL_SHM_FORMAT_ARGB8888)
        {
            app->format = format;
        }
        break;
#elif (LV_COLOR_DEPTH == 16)
    case WL_SHM_FORMAT_RGB565:
        app->format = format;
        break;
#elif (LV_COLOR_DEPTH == 8)
    case WL_SHM_FORMAT_RGB332:
        app->format = format;
        break;
#elif (LV_COLOR_DEPTH == 1)
    case WL_SHM_FORMAT_RGB332:
        app->format = format;
        break;
#endif
    default:
        break;
    }
}

static const struct wl_shm_listener shm_listener = {
    shm_format
};

static void pointer_handle_enter(void *data, struct wl_pointer *pointer,
                                 uint32_t serial, struct wl_surface *surface,
                                 wl_fixed_t sx, wl_fixed_t sy)
{
    struct application *app = data;
    const char * cursor = "left_ptr";

    if (!surface)
    {
        app->pointer = NULL;
        return;
    }

    app->pointer = wl_surface_get_user_data(surface);

    app->pointer->mouse.x = wl_fixed_to_int(sx);
    app->pointer->mouse.y = wl_fixed_to_int(sy);

    switch (app->pointer->parent_type)
    {
#if LV_WAYLAND_CLIENT_SIDE_DECORATIONS
    case PARENT_IS_DECORATION:
#if LV_WAYLAND_XDG_SHELL
        if (app->pointer->window->xdg_toplevel)
        {
            cursor = "grabbing";
        }
#endif
        break;
#endif
    default:
        break;
    }

    if (app->cursor_surface)
    {
        struct wl_cursor_image *cursor_image = wl_cursor_theme_get_cursor(app->cursor_theme, cursor)->images[0];
        wl_pointer_set_cursor(pointer, serial, app->cursor_surface, cursor_image->hotspot_x, cursor_image->hotspot_y);
        wl_surface_attach(app->cursor_surface, wl_cursor_image_get_buffer(cursor_image), 0, 0);
        wl_surface_damage(app->cursor_surface, 0, 0, cursor_image->width, cursor_image->height);
        wl_surface_commit(app->cursor_surface);
        app->cursor_flush_pending = true;
    }
}

static void pointer_handle_leave(void *data, struct wl_pointer *pointer,
                                 uint32_t serial, struct wl_surface *surface)
{
    struct application *app = data;

    if (!surface || (app->pointer == wl_surface_get_user_data(surface)))
    {
        app->pointer = NULL;
    }
}

static void pointer_handle_motion(void *data, struct wl_pointer *pointer,
                                  uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
{
    struct application *app = data;
    int max_x, max_y;

    if (!app->pointer)
    {
        return;
    }

    switch (app->pointer->parent_type)
    {
    case PARENT_IS_WINDOW:
        max_x = (app->pointer->window->width - 1);
        max_y = (app->pointer->window->height - 1);
        break;
#if LV_WAYLAND_CLIENT_SIDE_DECORATIONS
    case PARENT_IS_DECORATION:
        max_x = (app->pointer->window->width - 1);
        max_y = TITLE_BAR_HEIGHT;
        break;
    case PARENT_IS_BUTTON:
        max_x = BUTTON_SIZE;
        max_y = BUTTON_SIZE;
        break;
#endif
    default:
        max_x = 0;
        max_y = 0;
        break;
    }

    app->pointer->mouse.x = LV_MAX(0, LV_MIN(wl_fixed_to_int(sx), max_x));
    app->pointer->mouse.y = LV_MAX(0, LV_MIN(wl_fixed_to_int(sy), max_y));
}

static void pointer_handle_button(void *data, struct wl_pointer *wl_pointer,
                                  uint32_t serial, uint32_t time, uint32_t button,
                                  uint32_t state)
{
    struct application *app = data;
    const lv_indev_state_t lv_state =
        (state == WL_POINTER_BUTTON_STATE_PRESSED) ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;

    if (!app->pointer)
    {
        return;
    }

#if LV_WAYLAND_CLIENT_SIDE_DECORATIONS
    struct window *window = app->pointer->window;
    if (app->pointer->parent_type == PARENT_IS_DECORATION)
    {
#if LV_WAYLAND_XDG_SHELL
        if (window->xdg_toplevel)
        {
            xdg_toplevel_move(window->xdg_toplevel, app->wl_seat, serial);
            window->flush_pending = true;
        }
#endif
    }
    else if (app->pointer->parent_type == PARENT_IS_BUTTON)
    {
        if (state == WL_POINTER_BUTTON_STATE_RELEASED)
        {
            struct button *button = input_to_parent(app->pointer, struct button);
            switch (button->type)
            {
            case BUTTON_TYPE_CLOSE:
                window->shall_close = true;
                break;
            case BUTTON_TYPE_MINIMIZE:
#if LV_WAYLAND_XDG_SHELL
                if (window->xdg_toplevel)
                {
                    xdg_toplevel_set_minimized(window->xdg_toplevel);
                    window->flush_pending = true;
                }
#endif
                break;
            default:
                break;
            }
        }
    }
    else
#endif
    {
        switch (button & 0xF)
        {
        case 0:
            app->pointer->mouse.left_button = lv_state;
            break;
        case 1:
            app->pointer->mouse.right_button = lv_state;
            break;
        case 2:
            app->pointer->mouse.wheel_button = lv_state;
            break;
        default:
            break;
        }
    }
}

static void pointer_handle_axis(void *data, struct wl_pointer *wl_pointer,
                                uint32_t time, uint32_t axis, wl_fixed_t value)
{
    struct application *app = data;
    const int diff = wl_fixed_to_int(value);

    if (!app->pointer)
    {
        return;
    }

    if (axis == 0)
    {
        if (diff > 0)
        {
            app->pointer->mouse.wheel_diff++;
        }
        else if (diff < 0)
        {
            app->pointer->mouse.wheel_diff--;
        }
    }
}

static const struct wl_pointer_listener pointer_listener = {
    pointer_handle_enter,
    pointer_handle_leave,
    pointer_handle_motion,
    pointer_handle_button,
    pointer_handle_axis,
};

static lv_key_t keycode_xkb_to_lv(xkb_keysym_t xkb_key)
{
    lv_key_t key = 0;

    if (((xkb_key >= XKB_KEY_space) && (xkb_key <= XKB_KEY_asciitilde)))
    {
        key = xkb_key;
    }
    else if (((xkb_key >= XKB_KEY_KP_0) && (xkb_key <= XKB_KEY_KP_9)))
    {
        key = (xkb_key & 0x003f);
    }
    else
    {
        switch (xkb_key)
        {
        case XKB_KEY_BackSpace:
            key = LV_KEY_BACKSPACE;
            break;
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter:
            key = LV_KEY_ENTER;
            break;
        case XKB_KEY_Escape:
            key = LV_KEY_ESC;
            break;
        case XKB_KEY_Delete:
        case XKB_KEY_KP_Delete:
            key = LV_KEY_DEL;
            break;
        case XKB_KEY_Home:
        case XKB_KEY_KP_Home:
            key = LV_KEY_HOME;
            break;
        case XKB_KEY_Left:
        case XKB_KEY_KP_Left:
            key = LV_KEY_LEFT;
            break;
        case XKB_KEY_Up:
        case XKB_KEY_KP_Up:
            key = LV_KEY_UP;
            break;
        case XKB_KEY_Right:
        case XKB_KEY_KP_Right:
            key = LV_KEY_RIGHT;
            break;
        case XKB_KEY_Down:
        case XKB_KEY_KP_Down:
            key = LV_KEY_DOWN;
            break;
        case XKB_KEY_Prior:
        case XKB_KEY_KP_Prior:
            key = LV_KEY_PREV;
            break;
        case XKB_KEY_Next:
        case XKB_KEY_KP_Next:
        case XKB_KEY_Tab:
        case XKB_KEY_KP_Tab:
            key = LV_KEY_NEXT;
            break;
        case XKB_KEY_End:
        case XKB_KEY_KP_End:
            key = LV_KEY_END;
            break;
        default:
            break;
        }
    }

    return key;
}

static void keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard,
                                   uint32_t format, int fd, uint32_t size)
{
    struct application *app = data;

    struct xkb_keymap *keymap;
    struct xkb_state *state;
    char *map_str;

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
    {
        close(fd);
        return;
    }

    map_str = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map_str == MAP_FAILED)
    {
        close(fd);
        return;
    }

    /* Set up XKB keymap */
    keymap = xkb_keymap_new_from_string(app->xkb_context, map_str,
                                        XKB_KEYMAP_FORMAT_TEXT_V1, 0);
    munmap(map_str, size);
    close(fd);

    if (!keymap)
    {
        LV_LOG_ERROR("failed to compile keymap\n");
        return;
    }

    /* Set up XKB state */
    state = xkb_state_new(keymap);
    if (!state)
    {
        LV_LOG_ERROR("failed to create XKB state\n");
        xkb_keymap_unref(keymap);
        return;
    }

    xkb_keymap_unref(app->seat.xkb.keymap);
    xkb_state_unref(app->seat.xkb.state);
    app->seat.xkb.keymap = keymap;
    app->seat.xkb.state = state;
}

static void keyboard_handle_enter(void *data, struct wl_keyboard *keyboard,
                                  uint32_t serial, struct wl_surface *surface,
                                  struct wl_array *keys)
{
    struct application *app = data;
    struct input *input;

    if (!surface)
    {
        app->keyboard = NULL;
    }
    else
    {
        app->keyboard = wl_surface_get_user_data(surface);
    }
}

static void keyboard_handle_leave(void *data, struct wl_keyboard *keyboard,
                                  uint32_t serial, struct wl_surface *surface)
{
    struct application *app = data;

    if (!surface || (app->keyboard == wl_surface_get_user_data(surface)))
    {
        app->keyboard = NULL;
    }
}

static void keyboard_handle_key(void *data, struct wl_keyboard *keyboard,
                                uint32_t serial, uint32_t time, uint32_t key,
                                uint32_t state)
{
    struct application *app = data;
    const uint32_t code = (key + 8);
    const xkb_keysym_t *syms;
    xkb_keysym_t sym = XKB_KEY_NoSymbol;

    if (!app->keyboard || !app->seat.xkb.state)
    {
        return;
    }

    if (xkb_state_key_get_syms(app->seat.xkb.state, code, &syms) == 1)
    {
        sym = syms[0];
    }

    const lv_key_t lv_key = keycode_xkb_to_lv(sym);
    const lv_indev_state_t lv_state =
        (state == WL_KEYBOARD_KEY_STATE_PRESSED) ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;

    if (lv_key != 0)
    {
        app->keyboard->keyboard.key = lv_key;
        app->keyboard->keyboard.state = lv_state;
    }
}

static void keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard,
                                      uint32_t serial, uint32_t mods_depressed,
                                      uint32_t mods_latched, uint32_t mods_locked,
                                      uint32_t group)
{
    struct application *app = data;

    /* If we're not using a keymap, then we don't handle PC-style modifiers */
    if (!app->seat.xkb.keymap)
    {
        return;
    }

    xkb_state_update_mask(app->seat.xkb.state,
                          mods_depressed, mods_latched, mods_locked, 0, 0, group);
}

static const struct wl_keyboard_listener keyboard_listener = {
    keyboard_handle_keymap,
    keyboard_handle_enter,
    keyboard_handle_leave,
    keyboard_handle_key,
    keyboard_handle_modifiers,
};

static void touch_handle_down(void *data, struct wl_touch *wl_touch,
                              uint32_t serial, uint32_t time, struct wl_surface *surface,
                              int32_t id, wl_fixed_t x_w, wl_fixed_t y_w)
{
    struct application *app = data;

    if (!surface)
    {
        app->touch = NULL;
        return;
    }

    app->touch = wl_surface_get_user_data(surface);

    app->touch->touch.x = wl_fixed_to_int(x_w);
    app->touch->touch.y = wl_fixed_to_int(y_w);
    app->touch->touch.state = LV_INDEV_STATE_PR;
}

static void touch_handle_up(void *data, struct wl_touch *wl_touch,
                            uint32_t serial, uint32_t time, int32_t id)
{
    struct application *app = data;

    if (!app->touch)
    {
        return;
    }

    app->touch->touch.state = LV_INDEV_STATE_REL;
    app->touch = NULL;
}

static void touch_handle_motion(void *data, struct wl_touch *wl_touch,
                                uint32_t time, int32_t id, wl_fixed_t x_w, wl_fixed_t y_w)
{
    struct application *app = data;

    if (!app->touch)
    {
        return;
    }

    app->touch->touch.x = wl_fixed_to_int(x_w);
    app->touch->touch.y = wl_fixed_to_int(y_w);
}

static void touch_handle_frame(void *data, struct wl_touch *wl_touch)
{
}

static void touch_handle_cancel(void *data, struct wl_touch *wl_touch)
{
}

static const struct wl_touch_listener touch_listener = {
    touch_handle_down,
    touch_handle_up,
    touch_handle_motion,
    touch_handle_frame,
    touch_handle_cancel,
};

static void seat_handle_capabilities(void *data, struct wl_seat *wl_seat, enum wl_seat_capability caps)
{
    struct application *app = data;
    struct seat *seat = &app->seat;

    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !seat->wl_pointer)
    {
        seat->wl_pointer = wl_seat_get_pointer(wl_seat);
        wl_pointer_add_listener(seat->wl_pointer, &pointer_listener, app);
        app->cursor_surface = wl_compositor_create_surface(app->compositor);
        if (!app->cursor_surface)
        {
            LV_LOG_WARN("failed to create cursor surface");
        }
    }
    else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && seat->wl_pointer)
    {
        wl_pointer_destroy(seat->wl_pointer);
        if (app->cursor_surface)
        {
            wl_surface_destroy(app->cursor_surface);
        }
        seat->wl_pointer = NULL;
    }

    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !seat->wl_keyboard)
    {
        seat->wl_keyboard = wl_seat_get_keyboard(wl_seat);
        wl_keyboard_add_listener(seat->wl_keyboard, &keyboard_listener, app);
    }
    else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && seat->wl_keyboard)
    {
        wl_keyboard_destroy(seat->wl_keyboard);
        seat->wl_keyboard = NULL;
    }

    if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !seat->wl_touch)
    {
        seat->wl_touch = wl_seat_get_touch(wl_seat);
        wl_touch_add_listener(seat->wl_touch, &touch_listener, app);
    }
    else if (!(caps & WL_SEAT_CAPABILITY_TOUCH) && seat->wl_touch)
    {
        wl_touch_destroy(seat->wl_touch);
        seat->wl_touch = NULL;
    }
}

static const struct wl_seat_listener seat_listener = {
    seat_handle_capabilities,
};

#if LV_WAYLAND_WL_SHELL
static void wl_shell_handle_ping(void *data, struct wl_shell_surface *shell_surface, uint32_t serial)
{
    wl_shell_surface_pong(shell_surface, serial);
}

static void wl_shell_handle_configure(void *data, struct wl_shell_surface *shell_surface,
                                      uint32_t edges, int32_t width, int32_t height)
{

}

static void wl_shell_handle_popup_done(void *data, struct wl_shell_surface *shell_surface)
{

}

static const struct wl_shell_surface_listener shell_surface_listener = {
    wl_shell_handle_ping,
    wl_shell_handle_configure,
    wl_shell_handle_popup_done
};
#endif

#if LV_WAYLAND_XDG_SHELL
static void xdg_surface_handle_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial)
{
    xdg_surface_ack_configure(xdg_surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_handle_configure,
};

void xdg_toplevel_handle_configure(void *data, struct xdg_toplevel *xdg_toplevel,
                                   int32_t width, int32_t height, struct wl_array *states)
{
    struct window *window = (struct window *)data;
    if ((width == 0) || (height == 0))
    {
        return;
    }
    // TODO: window_resize(window, width, height, true);
}

static void xdg_toplevel_handle_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
    struct window *window = (struct window *)data;
    window->shall_close = true;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_handle_configure,
    .close = xdg_toplevel_handle_close,
};

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial)
{
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping
};
#endif

#if LV_WAYLAND_IVI_APPLICATION
static void ivi_surface_handle_configure(void *data, struct ivi_surface *ivi_surface,
                                         int32_t width, int32_t height)
{
    struct window *window = (struct window *)data;

    // TODO: window_resize(window, width, height, true);
}

static const struct ivi_surface_listener ivi_surface_listener = {
    .configure = ivi_surface_handle_configure
};
#endif

static void handle_global(void *data, struct wl_registry *registry,
                          uint32_t name, const char *interface, uint32_t version)
{
    struct application *app = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0)
    {
        app->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
    }
    else if (strcmp(interface, wl_subcompositor_interface.name) == 0)
    {
        app->subcompositor = wl_registry_bind(registry, name, &wl_subcompositor_interface, 1);
    }
    else if (strcmp(interface, wl_shm_interface.name) == 0)
    {
        app->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
        wl_shm_add_listener(app->shm, &shm_listener, app);
        app->cursor_theme = wl_cursor_theme_load(NULL, 32, app->shm);
    }
    else if (strcmp(interface, wl_seat_interface.name) == 0)
    {
        app->wl_seat = wl_registry_bind(app->registry, name, &wl_seat_interface, 1);
        wl_seat_add_listener(app->wl_seat, &seat_listener, app);
    }
#if LV_WAYLAND_WL_SHELL
    else if (strcmp(interface, wl_shell_interface.name) == 0)
    {
        app->wl_shell = wl_registry_bind(registry, name, &wl_shell_interface, 1);
    }
#endif
#if LV_WAYLAND_XDG_SHELL
    else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
    {
        app->xdg_wm = wl_registry_bind(app->registry, name, &xdg_wm_base_interface, version);
        xdg_wm_base_add_listener(app->xdg_wm, &xdg_wm_base_listener, app);
    }
#endif
#if LV_WAYLAND_IVI_APPLICATION
    else if (strcmp(interface, ivi_application_interface.name) == 0)
    {
        app->ivi_application = wl_registry_bind(app->registry, name, &ivi_application_interface, version);
    }
#endif
}

static void handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{

}

static const struct wl_registry_listener registry_listener = {
    handle_global,
    handle_global_remove
};

#if LV_WAYLAND_CLIENT_SIDE_DECORATIONS
static struct decoration * create_titlebar(struct application *app, struct window *window,
                                           unsigned int title_bar_height)
{
    unsigned int size = (window->width * title_bar_height * BYTES_PER_PIXEL);
    struct decoration *decoration;

    decoration = lv_mem_alloc(sizeof(struct decoration));
    LV_ASSERT_MALLOC(decoration);
    if (!decoration)
    {
        return NULL;
    }

    decoration->data = window->data + window->data_offset;

    decoration->surface = wl_compositor_create_surface(app->compositor);
    if (!decoration->surface)
    {
        LV_LOG_ERROR("cannot create surface for decoration");
        goto err_out;
    }

    decoration->input.window = window;
    decoration->input.parent_type = PARENT_IS_DECORATION;
    wl_surface_set_user_data(decoration->surface, &decoration->input);

    decoration->subsurface = wl_subcompositor_get_subsurface(app->subcompositor,
                                                             decoration->surface, window->surface);
    if (!decoration->subsurface)
    {
        LV_LOG_ERROR("cannot get subsurface for decoration");
        goto err_destroy_surface;
    }

    wl_subsurface_set_desync(decoration->subsurface);
    wl_subsurface_set_position(decoration->subsurface, 0, -title_bar_height);

    decoration->buffer = wl_shm_pool_create_buffer(window->shm_pool, window->data_offset,
                                                   window->width, title_bar_height,
                                                   (window->width * BYTES_PER_PIXEL),
                                                   app->format);
    if (!decoration->buffer)
    {
        LV_LOG_ERROR("cannot create buffer for decoration");
        goto err_destroy_subsurface;
    }

    window->data_offset += size;

    lv_color_fill((lv_color_t *)decoration->data, lv_color_make(0x66, 0x66, 0x66),
                  (window->width * title_bar_height));

    wl_surface_attach(decoration->surface, decoration->buffer, 0, 0);
    wl_surface_commit(decoration->surface);

    return decoration;

err_destroy_subsurface:
    wl_subsurface_destroy(decoration->subsurface);

err_destroy_surface:
    wl_surface_destroy(decoration->surface);

err_out:
    return NULL;
}

static void destroy_titlebar(struct decoration * titlebar)
{
    lv_mem_free(titlebar);
}

static struct button * create_button(struct application *app, struct window *window,
                                     unsigned int button_size, unsigned int margin,
                                     enum button_type type)
{
    unsigned int size = (button_size * button_size * BYTES_PER_PIXEL);
    struct button *button;

    button = lv_mem_alloc(sizeof(struct button));
    LV_ASSERT_MALLOC(button);
    if (!button)
    {
        return NULL;
    }

    button->data = window->data + window->data_offset;
    button->type = type;

    button->surface = wl_compositor_create_surface(app->compositor);
    if (!button->surface)
    {
        LV_LOG_ERROR("cannot create surface for button");
        goto err_out;
    }

    button->input.window = window;
    button->input.parent_type = PARENT_IS_BUTTON;
    wl_surface_set_user_data(button->surface, &button->input);

    button->subsurface = wl_subcompositor_get_subsurface(app->subcompositor,
                                                         button->surface, window->surface);
    if (!button->subsurface)
    {
        LV_LOG_ERROR("cannot get subsurface for button");
        goto err_destroy_surface;
    }

    wl_subsurface_set_desync(button->subsurface);
    wl_subsurface_set_position(button->subsurface,
                               window->width - ((button_size + margin) * (type + 1)),
                               -(button_size + margin));

    button->buffer = wl_shm_pool_create_buffer(window->shm_pool, window->data_offset,
                                               button_size, button_size,
                                               (button_size * BYTES_PER_PIXEL),
                                               app->format);
    if (!button->buffer)
    {
        LV_LOG_ERROR("cannot create buffer for button");
        goto err_destroy_subsurface;
    }

    window->data_offset += size;

    lv_color_fill((lv_color_t *)button->data,
                  lv_color_make(0xCC, 0xCC, 0xCC), (button_size * button_size));
    int x, y;
    switch (type)
    {
    case BUTTON_TYPE_CLOSE:
        for (y = 0; y < button_size; y++)
        {
            for (x = 0; x < button_size; x++)
            {
                lv_color_t *pixel = ((lv_color_t *)button->data + (y * button_size) + x);
                if ((x >= BUTTON_PADDING) && (x < button_size - BUTTON_PADDING))
                {
                    if ((x == y) || (x == button_size - 1 - y))
                    {
                        *pixel = lv_color_make(0x33, 0x33, 0x33);
                    }
                    else if ((x == y - 1) || (x == button_size - y))
                    {
                        *pixel = lv_color_make(0x66, 0x66, 0x66);
                    }
                }
            }
        }
        break;
    case BUTTON_TYPE_MINIMIZE:
        for (y = 0; y < button_size; y++)
        {
            for (x = 0; x < button_size; x++)
            {
                lv_color_t *pixel = ((lv_color_t *)button->data + (y * button_size) + x);
                if ((x >= BUTTON_PADDING) && (x < button_size - BUTTON_PADDING) &&
                    (y > button_size - (2 * BUTTON_PADDING)) && (y < button_size - BUTTON_PADDING))
                {
                    *pixel = lv_color_make(0x33, 0x33, 0x33);
                }
            }
        }
        break;
    default:
        LV_ASSERT_MSG(0, "Invalid button");
        goto err_destroy_surface;
    }

    wl_surface_attach(button->surface, button->buffer, 0, 0);
    wl_surface_commit(button->surface);

    return button;

err_destroy_subsurface:
    wl_subsurface_destroy(button->subsurface);

err_destroy_surface:
    wl_surface_destroy(button->surface);

err_out:
    return NULL;
}

static void destroy_button(struct button * button)
{
    lv_mem_free(button);
}
#endif

static struct window *create_window(struct application *app, int width, int height, const char *title)
{
    unsigned int size = (width * height * BYTES_PER_PIXEL);
    static const char template[] = "/lvgl-wayland-XXXXXX";
    struct window *window;
    char *name;
    int ret;
    int fd;

    window = _lv_ll_ins_tail(&app->window_ll);
    LV_ASSERT_MALLOC(window);
    if (!window)
    {
        return NULL;
    }

    lv_memset(window, 0x00, sizeof(struct window));

    window->width = width;
    window->height = height;
    window->application = app;
    window->data_size = size;

#if LV_WAYLAND_CLIENT_SIDE_DECORATIONS
    window->data_size += (width * TITLE_BAR_HEIGHT * BYTES_PER_PIXEL);

    int b;
    for (b = 0; b < NUM_BUTTONS; b++)
    {
        window->data_size += (BUTTON_SIZE * BUTTON_SIZE * BYTES_PER_PIXEL);
    }
#endif

    name = lv_mem_alloc(strlen(app->xdg_runtime_dir) + sizeof(template));
    if (!name)
    {
        LV_LOG_ERROR("cannot allocate memory for name: %s\n", strerror(errno));
        goto err_free_window;
    }

    strcpy(name, app->xdg_runtime_dir);
    strcat(name, template);

    fd = mkstemp(name);

    lv_mem_free(name);

    if (fd < 0)
    {
        LV_LOG_ERROR("cannot create tmpfile: %s\n", strerror(errno));
        goto err_free_window;
    }

    do
    {
        ret = ftruncate(fd, window->data_size);
    }
    while ((ret < 0) && (errno == EINTR));

    if (ret < 0)
    {
        LV_LOG_ERROR("ftruncate failed: %s\n", strerror(errno));
        goto err_close_fd;
    }

    window->data = mmap(NULL, window->data_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (window->data == MAP_FAILED)
    {
        LV_LOG_ERROR("mmap failed: %s\n", strerror(errno));
        goto err_close_fd;
    }

    window->shm_pool = wl_shm_create_pool(app->shm, fd, window->data_size);
    if (!window->shm_pool)
    {
        LV_LOG_ERROR("cannot create shm pool\n");
        goto err_unmap;
    }

    window->buffer = wl_shm_pool_create_buffer(window->shm_pool, 0,
                                               width, height,
                                               width * BYTES_PER_PIXEL,
                                               app->format);
    if (!window->buffer)
    {
        LV_LOG_ERROR("cannot create shm buffer\n");
        goto err_destroy_pool;
    }

    window->data_offset += (width * height * BYTES_PER_PIXEL);

    // Create compositor surface
    window->surface = wl_compositor_create_surface(app->compositor);
    if (!window->surface)
    {
        LV_LOG_ERROR("cannot create surface");
        goto err_destroy_buffer;
    }

    window->input.window = window;
    window->input.parent_type = PARENT_IS_WINDOW;
    wl_surface_set_user_data(window->surface, &window->input);

    // Create shell surface
    if (0)
    {
        // Needed for #if madness below
    }
#if LV_WAYLAND_IVI_APPLICATION
    else if (app->ivi_application)
    {
        uint32_t ivi_id = app->ivi_id_base;
        struct window *prev_window = _lv_ll_get_prev(&app->window_ll, window);
        while (prev_window != NULL)
        {
            ivi_id++;
            prev_window = _lv_ll_get_prev(&app->window_ll, window);
        }

        window->ivi_surface = ivi_application_surface_create(app->ivi_application,
                                                             ivi_id, window->surface);
        if (!window->ivi_surface)
        {
            LV_LOG_ERROR("cannot create IVI surface");
            goto err_destroy_surface;
        }
        else
        {
            LV_LOG_INFO("created IVI surface with ID %u", ivi_id);
        }

        ivi_surface_add_listener(window->ivi_surface, &ivi_surface_listener, window);
    }
#endif
#if LV_WAYLAND_XDG_SHELL
    else if (app->xdg_wm)
    {
        window->xdg_surface = xdg_wm_base_get_xdg_surface(app->xdg_wm, window->surface);
        if (!window->xdg_surface)
        {
            LV_LOG_ERROR("cannot create XDG surface");
            goto err_destroy_surface;
        }

        xdg_surface_add_listener(window->xdg_surface, &xdg_surface_listener, window);

        window->xdg_toplevel = xdg_surface_get_toplevel(window->xdg_surface);
        if (!window->xdg_toplevel)
        {
            LV_LOG_ERROR("cannot get XDG toplevel surface");
            goto err_destroy_shell_surface;
        }

        xdg_toplevel_add_listener(window->xdg_toplevel, &xdg_toplevel_listener, window);
        xdg_toplevel_set_title(window->xdg_toplevel, title);
        xdg_toplevel_set_app_id(window->xdg_toplevel, title);
    }
#endif
#if LV_WAYLAND_WL_SHELL
    else if (app->wl_shell)
    {
        window->wl_shell_surface = wl_shell_get_shell_surface(app->wl_shell, window->surface);
        if (!window->wl_shell_surface)
        {
            LV_LOG_ERROR("cannot create WL shell surface");
            goto err_destroy_surface;
        }

        wl_shell_surface_add_listener(window->wl_shell_surface, &shell_surface_listener, &window);
        wl_shell_surface_set_toplevel(window->wl_shell_surface);
        wl_shell_surface_set_title(window->wl_shell_surface, title);
    }
#endif
    else
    {
        LV_LOG_ERROR("No shell available");
        goto err_destroy_surface;
    }

#if LV_WAYLAND_CLIENT_SIDE_DECORATIONS
    if (!app->opt_disable_decorations)
    {
        window->decoration[0] = create_titlebar(app, window, TITLE_BAR_HEIGHT);
        if (!window->decoration[0])
        {
            LV_LOG_ERROR("Failed to create titlebar");
        }
        else
        {
            for (b = 0; b < NUM_BUTTONS; b++)
            {
                /* Create only meaningful butttons */
                switch (b)
                {
                case BUTTON_TYPE_CLOSE:
                    break;
#if LV_WAYLAND_XDG_SHELL
                case BUTTON_TYPE_MINIMIZE:
                    if (window->xdg_toplevel)
                    {
                        break;
                    }
                    continue;
#endif
                default:
                    continue;
                }
                window->button[b] = create_button(app, window, BUTTON_SIZE, BUTTON_MARGIN, b);
                if (!window->button[b])
                {
                    LV_LOG_ERROR("Failed to create button %d", b);
                }
            }
        }
    }
#endif

    close(fd);

    return window;

err_destroy_shell_surface2:
#if LV_WAYLAND_XDG_SHELL
    if (window->xdg_toplevel)
    {
        xdg_toplevel_destroy(window->xdg_toplevel);
    }
#endif

err_destroy_shell_surface:
#if LV_WAYLAND_WL_SHELL
    if (window->wl_shell_surface)
    {
        wl_shell_surface_destroy(window->wl_shell_surface);
    }
#endif
#if LV_WAYLAND_XDG_SHELL
    if (window->xdg_surface)
    {
        xdg_surface_destroy(window->xdg_surface);
    }
#endif
#if LV_WAYLAND_IVI_APPLICATION
    if (window->ivi_surface)
    {
        ivi_surface_destroy(window->ivi_surface);
    }
#endif

err_destroy_surface:
    wl_surface_destroy(window->surface);

err_destroy_buffer:
    wl_buffer_destroy(window->buffer);

err_destroy_pool:
    wl_shm_pool_destroy(window->shm_pool);

err_unmap:
    munmap(window->data, window->data_size);

err_close_fd:
    close(fd);

err_free_window:
    _lv_ll_remove(&app->window_ll, window);
    lv_mem_free(window);
    return NULL;
}

static void destroy_window(struct window *window)
{
    if (!window)
    {
        return;
    }

#if LV_WAYLAND_WL_SHELL
    if (window->wl_shell_surface)
    {
        wl_shell_surface_destroy(window->wl_shell_surface);
    }
#endif
#if LV_WAYLAND_XDG_SHELL
    if (window->xdg_toplevel)
    {
        xdg_toplevel_destroy(window->xdg_toplevel);
        xdg_surface_destroy(window->xdg_surface);
    }
#endif
#if LV_WAYLAND_IVI_APPLICATION
    if (window->ivi_surface)
    {
        ivi_surface_destroy(window->ivi_surface);
    }
#endif

    wl_surface_destroy(window->surface);

    wl_buffer_destroy(window->buffer);

    wl_shm_pool_destroy(window->shm_pool);

    munmap(window->data, window->data_size);

#if LV_WAYLAND_CLIENT_SIDE_DECORATIONS
    if (window->decoration[0] != NULL)
    {
        int b;
        destroy_titlebar(window->decoration[0]);
        for (b = 0; b < NUM_BUTTONS; b++)
        {
            if (window->button[b])
            {
                destroy_button(window->button[b]);
            }
        }
    }
#endif
}

/**
 * Dispath Wayland events and flush changes to server
 */
static void window_cycle(lv_disp_drv_t *disp_drv, uint32_t time, uint32_t px)
{
    struct window *window = disp_drv->user_data;
    struct application *app = window->application;
    bool shall_flush = app->cursor_flush_pending;

    window->cycled = true;
    if (window->shall_close)
    {
        destroy_window(window);
        window->closed = true;
        window->shall_close = false;
        window->flush_pending = true;
    }
    else if (window->ext_monitor_cb)
    {
        window->ext_monitor_cb(disp_drv, time, px);
    }

    // If at least one window has not yet been cycled, stop here
    struct window *a_window;
    _LV_LL_READ(&app->window_ll, a_window)
    {
        if (!a_window->cycled)
        {
            return;
        }
    }

    // Check if flush has to be performed and reset flush and cycle flags
    _LV_LL_READ(&app->window_ll, a_window)
    {
        shall_flush |= a_window->flush_pending;
        a_window->flush_pending = false;
        a_window->cycled = false;
    }

    // Flush changes to Wayland compositor and read events from it
    while (wl_display_prepare_read(app->display) != 0)
    {
        wl_display_dispatch_pending(app->display);
    }

    if (shall_flush)
    {
        wl_display_flush(app->display);
        app->cursor_flush_pending = false;
    }

    wl_display_read_events(app->display);
    wl_display_dispatch_pending(app->display);

    // Check if at least one window is not closed
    _LV_LL_READ(&app->window_ll, a_window)
    {
        if (!a_window->closed)
        {
            return;
        }
    }

    // If all windows have been closed, terminate execution
    exit(0);
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Initialize Wayland driver
 */
void lv_wayland_init(void)
{
    // Create XKB context
    application.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    LV_ASSERT_MSG(application.xkb_context, "failed to create XKB context");
    if (application.xkb_context == NULL)
    {
        return;
    }

    // Connect to Wayland display
    application.display = wl_display_connect(NULL);
    LV_ASSERT_MSG(application.display, "failed to connect to Wayland server");
    if (application.display == NULL)
    {
        return;
    }

    /* Add registry listener and wait for registry reception */
    application.format = 0xFFFFFFFF;
    application.registry = wl_display_get_registry(application.display);
    wl_registry_add_listener(application.registry, &registry_listener, &application);
    wl_display_dispatch(application.display);
    wl_display_roundtrip(application.display);

    LV_ASSERT_MSG((application.format != 0xFFFFFFFF), "WL_SHM_FORMAT not available");
    if (application.format == 0xFFFFFFFF)
    {
        return;
    }

    application.xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
    LV_ASSERT_MSG(application.xdg_runtime_dir, "cannot get XDG_RUNTIME_DIR");

#ifdef LV_WAYLAND_CLIENT_SIDE_DECORATIONS
    const char * env_disable_decorations = getenv("LV_WAYLAND_DISABLE_WINDOWDECORATION");
    application.opt_disable_decorations = ((env_disable_decorations != NULL) &&
                                           (env_disable_decorations[0] != '0'));
#endif

#if LV_WAYLAND_IVI_APPLICATION
    const char * env_ivi_id = getenv("LV_WAYLAND_IVI_ID");
    if ((env_ivi_id != NULL) && _is_digit(env_ivi_id[0]))
    {
        application.ivi_id_base = _atoi(&env_ivi_id);
    }
    else
    {
        application.ivi_id_base = LV_WAYLAND_IVI_ID_BASE;
    }
#endif

    _lv_ll_init(&application.window_ll, sizeof(struct window));
}

/**
 * De-initialize Wayland driver
 */
void lv_wayland_deinit(void)
{
    struct window *window = NULL;

    _LV_LL_READ(&application.window_ll, window)
    {
        if (!window->closed)
        {
            destroy_window(window);
        }
    }

    if (application.shm)
    {
        wl_shm_destroy(application.shm);
    }

#if LV_WAYLAND_XDG_SHELL
    if (application.xdg_wm)
    {
        xdg_wm_base_destroy(application.xdg_wm);
    }
#endif

#if LV_WAYLAND_WL_SHELL
    if (application.wl_shell)
    {
        wl_shell_destroy(application.wl_shell);
    }
#endif

    if (application.wl_seat)
    {
        wl_seat_destroy(application.wl_seat);
    }

    if (application.subcompositor)
    {
        wl_subcompositor_destroy(application.subcompositor);
    }

    if (application.compositor)
    {
        wl_compositor_destroy(application.compositor);
    }

    wl_registry_destroy(application.registry);
    wl_display_flush(application.display);
    wl_display_disconnect(application.display);

    _lv_ll_clear(&application.window_ll);
}

/**
 * Flush a buffer to the marked area
 * @param drv pointer to driver where this function belongs
 * @param area an area where to copy `color_p`
 * @param color_p an array of pixel to copy to the `area` part of the screen
 */
void lv_wayland_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    struct window *window = disp_drv->user_data;
    lv_coord_t hres = (disp_drv->rotated == 0) ? (disp_drv->hor_res) : (disp_drv->ver_res);
    lv_coord_t vres = (disp_drv->rotated == 0) ? (disp_drv->ver_res) : (disp_drv->hor_res);

    /* If private data is not set, it means window has not been created yet */
    if (!window)
    {
        char title[64] = "LVGL";
        window = create_window(&application, hres, vres, title);
        if (!window)
        {
            LV_LOG_ERROR("failed to create wayland window\n");
            return;
        }
        disp_drv->user_data = window;
        /* Replace monitor_cb callback, saving the current one for further use */
        window->ext_monitor_cb = disp_drv->monitor_cb;
        disp_drv->monitor_cb = window_cycle;
    }
    /* If window has been / is being closed, or is not visible, skip rendering */
    else if (window->closed || window->shall_close)
    {
        lv_disp_flush_ready(disp_drv);
        return;
    }
    /* Return if the area is out the screen */
    else if ((area->x2 < 0) || (area->y2 < 0) || (area->x1 > hres - 1) || (area->y1 > vres - 1))
    {
        lv_disp_flush_ready(disp_drv);
        return;
    }

    int32_t x;
    int32_t y;
    for (y = area->y1; y <= area->y2 && y < disp_drv->ver_res; y++)
    {
        for (x = area->x1; x <= area->x2 && x < disp_drv->hor_res; x++)
        {
            int offset = (y * disp_drv->hor_res) + x;
#if (LV_COLOR_DEPTH == 32)
            uint32_t * const buf = (uint32_t *)window->data + offset;
            *buf = color_p->full;
#elif (LV_COLOR_DEPTH == 16)
            uint16_t * const buf = (uint16_t *)window->data + offset;
            *buf = color_p->full;
#elif (LV_COLOR_DEPTH == 8)
            uint8_t * const buf = (uint8_t *)window->data + offset;
            *buf = color_p->full;
#elif (LV_COLOR_DEPTH == 1)
            uint8_t * const buf = (uint8_t *)window->data + offset;
            *buf = ((0x07 * color_p->ch.red)   << 5) |
                   ((0x07 * color_p->ch.green) << 2) |
                   ((0x03 * color_p->ch.blue)  << 0);
#endif
            color_p++;
        }
    }

    wl_surface_attach(window->surface, window->buffer, 0, 0);
    wl_surface_damage(window->surface, area->x1, area->y1,
                      (area->x2 - area->x1 + 1), (area->y2 - area->y1 + 1));

    if (lv_disp_flush_is_last(disp_drv))
    {
        wl_surface_commit(window->surface);
        window->flush_pending = true;
    }

    lv_disp_flush_ready(disp_drv);
}

/**
 * Read pointer input
 * @param drv pointer to driver where this function belongs
 * @param data where to store input data
 */
void lv_wayland_pointer_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    struct window *window = drv->disp->driver->user_data;
    if (!window)
    {
        return;
    }

    data->point.x = window->input.mouse.x;
    data->point.y = window->input.mouse.y;
    data->state = window->input.mouse.left_button;
}

/**
 * Read axis input
 * @param drv pointer to driver where this function belongs
 * @param data where to store input data
 */
void lv_wayland_pointeraxis_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    struct window *window = drv->disp->driver->user_data;
    if (!window)
    {
        return;
    }

    data->state = window->input.mouse.wheel_button;
    data->enc_diff = window->input.mouse.wheel_diff;

    window->input.mouse.wheel_diff = 0;
}

/**
 * Read keyboard input
 * @param drv pointer to driver where this function belongs
 * @param data where to store input data
 */
void lv_wayland_keyboard_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    struct window *window = drv->disp->driver->user_data;
    if (!window)
    {
        return;
    }

    data->key = window->input.keyboard.key;
    data->state = window->input.keyboard.state;
}

/**
 * Read touch input
 * @param drv pointer to driver where this function belongs
 * @param data where to store input data
 */
void lv_wayland_touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    struct window *window = drv->disp->driver->user_data;
    if (!window)
    {
        return;
    }

    data->point.x = window->input.touch.x;
    data->point.y = window->input.touch.y;
    data->state = window->input.touch.state;
}
#endif // USE_WAYLAND
