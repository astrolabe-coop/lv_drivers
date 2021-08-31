/**
 * @file monitor.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "monitor.h"
#if USE_MONITOR

#ifndef MONITOR_SDL_INCLUDE_PATH
#  define MONITOR_SDL_INCLUDE_PATH <SDL2/SDL.h>
#endif

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include MONITOR_SDL_INCLUDE_PATH
#include "../indev/mouse.h"
#include "../indev/keyboard.h"
#include "../indev/mousewheel.h"

#include "../../lvgl/src/gpu/lv_gpu_sdl.h"

/*********************
 *      DEFINES
 *********************/
#define SDL_REFR_PERIOD     50  /*ms*/

#ifndef MONITOR_ZOOM
#define MONITOR_ZOOM        1
#endif

#ifndef MONITOR_HOR_RES
#define MONITOR_HOR_RES        LV_HOR_RES
#endif

#ifndef MONITOR_VER_RES
#define MONITOR_VER_RES        LV_VER_RES
#endif

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    SDL_Window * window;
    lv_disp_t * disp;
    volatile bool sdl_refr_qry;
}monitor_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void window_create(monitor_t * m);
static void window_update(monitor_t * m);
int quit_filter(void * userdata, SDL_Event * event);
static void monitor_sdl_clean_up(void);
static void monitor_sdl_init(void);
static void sdl_event_handler(lv_timer_t * t);

/***********************
 *   GLOBAL PROTOTYPES
 ***********************/

/**********************
 *  STATIC VARIABLES
 **********************/
monitor_t monitor;

#if MONITOR_DUAL
monitor_t monitor2;
#endif

static volatile bool sdl_inited = false;
static volatile bool sdl_quit_qry = false;


/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Initialize the monitor
 */
void monitor_init(void)
{
    monitor_sdl_init();
    lv_timer_create(sdl_event_handler, LV_INDEV_DEF_READ_PERIOD, NULL);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/


/**
 * SDL main thread. All SDL related task have to be handled here!
 * It initializes SDL, handles drawing and the mouse.
 */

static void sdl_event_handler(lv_timer_t * t)
{
    (void)t;

    /*Refresh handling*/
    SDL_Event event;
    while(SDL_PollEvent(&event)) {
#if USE_MOUSE != 0
        mouse_handler(&event);
#endif

#if USE_MOUSEWHEEL != 0
        mousewheel_handler(&event);
#endif

#if USE_KEYBOARD
        keyboard_handler(&event);
#endif
        if((&event)->type == SDL_WINDOWEVENT) {
            switch((&event)->window.event) {
#if SDL_VERSION_ATLEAST(2, 0, 5)
                case SDL_WINDOWEVENT_TAKE_FOCUS:
#endif
                case SDL_WINDOWEVENT_EXPOSED:
                    window_update(&monitor);
#if MONITOR_DUAL
                    window_update(&monitor2);
#endif
                    break;
                default:
                    break;
            }
        }
    }

    /*Run until quit event not arrives*/
    if(sdl_quit_qry) {
        monitor_sdl_clean_up();
        exit(0);
    }
}

int quit_filter(void * userdata, SDL_Event * event)
{
    (void)userdata;

    if(event->type == SDL_WINDOWEVENT) {
        if(event->window.event == SDL_WINDOWEVENT_CLOSE) {
            sdl_quit_qry = true;
        }
    }
    else if(event->type == SDL_QUIT) {
        sdl_quit_qry = true;
    }

    return 1;
}

static void monitor_sdl_clean_up(void)
{
    lv_sdl_display_deinit(monitor.disp);
    SDL_DestroyWindow(monitor.window);

#if MONITOR_DUAL
    lv_sdl_display_deinit(monitor2.disp);
    SDL_DestroyWindow(monitor2.window);

#endif

    SDL_Quit();
}

static void monitor_sdl_init(void)
{
    /*Initialize the SDL*/
    SDL_Init(SDL_INIT_VIDEO);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

    SDL_SetEventFilter(quit_filter, NULL);

    window_create(&monitor);
#if MONITOR_DUAL
    window_create(&monitor2);
    int x, y;
    SDL_GetWindowPosition(monitor2.window, &x, &y);
    SDL_SetWindowPosition(monitor.window, x + (MONITOR_HOR_RES * MONITOR_ZOOM) / 2 + 10, y);
    SDL_SetWindowPosition(monitor2.window, x - (MONITOR_HOR_RES * MONITOR_ZOOM) / 2 - 10, y);
#endif

    sdl_inited = true;
}


static void window_create(monitor_t * m)
{
    m->window = SDL_CreateWindow("TFT Simulator",
                              SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              MONITOR_HOR_RES * MONITOR_ZOOM, MONITOR_VER_RES * MONITOR_ZOOM, 0);       /*last param. SDL_WINDOW_BORDERLESS to hide borders*/

    m->disp = lv_sdl_display_init(m->window);

    m->sdl_refr_qry = true;

}

static void window_update(monitor_t * m)
{
    lv_obj_invalidate(lv_disp_get_scr_act(m->disp));
}

#endif /*USE_MONITOR*/
