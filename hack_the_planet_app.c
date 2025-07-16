#include <furi.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <furi_hal_adc.h>
#include <furi_hal_gpio.h>
#include <input/input.h>
#include <stdlib.h>
#include <stdbool.h>

// Constants
#define BUFFER_SIZE 128

// Forward declarations of callbacks
static void hack_the_planet_main_draw_callback(Canvas* canvas, void* context);
static bool hack_the_planet_main_input_callback(InputEvent* event, void* context);
static void hack_the_planet_about_draw_callback(Canvas* canvas, void* context);
static bool hack_the_planet_about_input_callback(InputEvent* event, void* context);
static void hack_the_planet_settings_draw_callback(Canvas* canvas, void* context);
static bool hack_the_planet_settings_input_callback(InputEvent* event, void* context);
static void hack_the_planet_submenu_callback(void* context, uint32_t index);
static void hack_the_planet_timer_callback(void* context);

// App states
typedef enum {
    APP_STATE_MENU,
    APP_STATE_RUNNING,
} AppState;

// Submenu indices
typedef enum {
    HackThePlanetSubmenuPlantMode = 0,
    HackThePlanetSubmenuSettings,
    HackThePlanetSubmenuAbout,
    HackThePlanetSubmenuCount,
} HackThePlanetSubmenu;

// Views enum
typedef enum {
    HackThePlanetViewSubmenu = 0,
    HackThePlanetViewMain,
    HackThePlanetViewAbout,
    HackThePlanetViewSettings,
    HackThePlanetViewCount,
} HackThePlanetView;

// Amplifier mode enum
typedef enum {
    AmplifierModeUnknown = 0,
    AmplifierModePlant,
} AmplifierMode;

// Main application structure
typedef struct {
    FuriMutex* mutex;
    float* voltage_buffer;
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    View* main_view;
    View* about_view;
    View* settings_view;
    FuriTimer* monitor_timer;
    AppState app_state;
    bool cleanup_in_progress;
    AmplifierMode amplifier_mode;
} HackThePlanetApp;

// GPIO initialization stub (fill with your GPIO setup)
static bool gpio_init(void) {
    // Initialize GPIOs here if needed
    return true;
}

// Timer callback stub (called periodically)
static void hack_the_planet_timer_callback(void* context) {
    HackThePlanetApp* app = context;
    // Add your ADC sampling and signal processing logic here
    (void)app; // avoid unused warning
}

// Main view draw callback
static void hack_the_planet_main_draw_callback(Canvas* canvas, void* context) {
    HackThePlanetApp* app = context;
    (void)app; // avoid unused warning

    canvas_clear(canvas);
    canvas_draw_str(canvas, 0, 10, "Hack The Planet - Plant Mode");
    canvas_draw_str(canvas, 0, 25, "Press Back to return to menu");
}

// Main view input callback
static bool hack_the_planet_main_input_callback(InputEvent* event, void* context) {
    HackThePlanetApp* app = context;

    if(event->type == InputTypePress) {
        if(event->key == InputKeyBack) {
            app->app_state = APP_STATE_MENU;
            view_dispatcher_switch_to_view(app->view_dispatcher, HackThePlanetViewSubmenu);
            return true;
        }
    }
    return false;
}

// About view draw callback
static void hack_the_planet_about_draw_callback(Canvas* canvas, void* context) {
    HackThePlanetApp* app = context;
    (void)app; // avoid unused warning

    canvas_clear(canvas);
    canvas_draw_str(canvas, 0, 10, "About Hack The Planet App");
    canvas_draw_str(canvas, 0, 25, "Plant signal monitoring");
    canvas_draw_str(canvas, 0, 40, "Press Back to return");
}

// About view input callback
static bool hack_the_planet_about_input_callback(InputEvent* event, void* context) {
    HackThePlanetApp* app = context;

    if(event->type == InputTypePress) {
        if(event->key == InputKeyBack) {
            view_dispatcher_switch_to_view(app->view_dispatcher, HackThePlanetViewSubmenu);
            return true;
        }
    }
    return false;
}

// Settings view draw callback
static void hack_the_planet_settings_draw_callback(Canvas* canvas, void* context) {
    HackThePlanetApp* app = context;
    (void)app; // avoid unused warning

    canvas_clear(canvas);
    canvas_draw_str(canvas, 0, 10, "Settings");
    canvas_draw_str(canvas, 0, 25, "No settings available");
    canvas_draw_str(canvas, 0, 40, "Press Back to return");
}

// Settings view input callback
static bool hack_the_planet_settings_input_callback(InputEvent* event, void* context) {
    HackThePlanetApp* app = context;

    if(event->type == InputTypePress) {
        if(event->key == InputKeyBack) {
            view_dispatcher_switch_to_view(app->view_dispatcher, HackThePlanetViewSubmenu);
            return true;
        }
    }
    return false;
}

// Submenu callback
static void hack_the_planet_submenu_callback(void* context, uint32_t index) {
    HackThePlanetApp* app = context;
    switch(index) {
    case HackThePlanetSubmenuPlantMode:
        app->app_state = APP_STATE_RUNNING;
        view_dispatcher_switch_to_view(app->view_dispatcher, HackThePlanetViewMain);
        break;
    case HackThePlanetSubmenuSettings:
        view_dispatcher_switch_to_view(app->view_dispatcher, HackThePlanetViewSettings);
        break;
    case HackThePlanetSubmenuAbout:
        view_dispatcher_switch_to_view(app->view_dispatcher, HackThePlanetViewAbout);
        break;
    default:
        break;
    }
}

// Detect hardware mode stub (you can implement detection here)
static AmplifierMode detect_hardware(HackThePlanetApp* app) {
    (void)app;
    return AmplifierModePlant;
}

// App allocation and initialization
static HackThePlanetApp* hack_the_planet_app_alloc(void) {
    HackThePlanetApp* app = malloc(sizeof(HackThePlanetApp));
    if(!app) return NULL;

    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    if(!app->mutex) {
        free(app);
        return NULL;
    }

    app->voltage_buffer = malloc(BUFFER_SIZE * sizeof(float));
    if(!app->voltage_buffer) {
        furi_mutex_free(app->mutex);
        free(app);
        return NULL;
    }

    if(!gpio_init()) {
        free(app->voltage_buffer);
        furi_mutex_free(app->mutex);
        free(app);
        return NULL;
    }

    app->gui = furi_record_open(RECORD_GUI);
    if(!app->gui) {
        free(app->voltage_buffer);
        furi_mutex_free(app->mutex);
        free(app);
        return NULL;
    }

    app->view_dispatcher = view_dispatcher_alloc();
    if(!app->view_dispatcher) {
        furi_record_close(RECORD_GUI);
        free(app->voltage_buffer);
        furi_mutex_free(app->mutex);
        free(app);
        return NULL;
    }

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->submenu = submenu_alloc();
    if(!app->submenu) {
        view_dispatcher_free(app->view_dispatcher);
        furi_record_close(RECORD_GUI);
        free(app->voltage_buffer);
        furi_mutex_free(app->mutex);
        free(app);
        return NULL;
    }

    // Add submenu items
    submenu_add_item(
        app->submenu,
        "Plant Mode",
        HackThePlanetSubmenuPlantMode,
        hack_the_planet_submenu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Settings",
        HackThePlanetSubmenuSettings,
        hack_the_planet_submenu_callback,
        app);
    submenu_add_item(
        app->submenu, "About", HackThePlanetSubmenuAbout, hack_the_planet_submenu_callback, app);

    app->main_view = view_alloc();
    view_set_draw_callback(app->main_view, hack_the_planet_main_draw_callback);
    view_set_input_callback(app->main_view, hack_the_planet_main_input_callback);
    view_set_context(app->main_view, app);

    app->about_view = view_alloc();
    view_set_draw_callback(app->about_view, hack_the_planet_about_draw_callback);
    view_set_input_callback(app->about_view, hack_the_planet_about_input_callback);
    view_set_context(app->about_view, app);

    app->settings_view = view_alloc();
    view_set_draw_callback(app->settings_view, hack_the_planet_settings_draw_callback);
    view_set_input_callback(app->settings_view, hack_the_planet_settings_input_callback);
    view_set_context(app->settings_view, app);

    view_dispatcher_add_view(
        app->view_dispatcher, HackThePlanetViewSubmenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(app->view_dispatcher, HackThePlanetViewMain, app->main_view);
    view_dispatcher_add_view(app->view_dispatcher, HackThePlanetViewAbout, app->about_view);
    view_dispatcher_add_view(app->view_dispatcher, HackThePlanetViewSettings, app->settings_view);

    view_dispatcher_switch_to_view(app->view_dispatcher, HackThePlanetViewSubmenu);

    app->monitor_timer =
        furi_timer_alloc(hack_the_planet_timer_callback, FuriTimerTypePeriodic, app);
    furi_timer_start(app->monitor_timer, 50); // 50ms interval

    app->app_state = APP_STATE_MENU;
    app->cleanup_in_progress = false;
    app->amplifier_mode = detect_hardware(app);

    return app;
}

// Cleanup
static void hack_the_planet_app_free(HackThePlanetApp* app) {
    if(!app) return;
    app->cleanup_in_progress = true;

    if(app->monitor_timer) {
        furi_timer_stop(app->monitor_timer);
        furi_timer_free(app->monitor_timer);
    }

    if(app->view_dispatcher) {
        view_dispatcher_remove_view(app->view_dispatcher, HackThePlanetViewMain);
        view_dispatcher_remove_view(app->view_dispatcher, HackThePlanetViewAbout);
        view_dispatcher_remove_view(app->view_dispatcher, HackThePlanetViewSettings);
        view_dispatcher_remove_view(app->view_dispatcher, HackThePlanetViewSubmenu);
        view_dispatcher_free(app->view_dispatcher);
    }

    if(app->submenu) {
        submenu_free(app->submenu);
    }

    if(app->main_view) {
        view_free(app->main_view);
    }
    if(app->about_view) {
        view_free(app->about_view);
    }
    if(app->settings_view) {
        view_free(app->settings_view);
    }

    if(app->voltage_buffer) {
        free(app->voltage_buffer);
    }
    if(app->mutex) {
        furi_mutex_free(app->mutex);
    }
    if(app->gui) {
        furi_record_close(RECORD_GUI);
    }

    free(app);
}

// Application entrypoint
int32_t hack_the_planet_app(void* p) {
    (void)p;
    HackThePlanetApp* app = hack_the_planet_app_alloc();
    if(!app) {
        return -1;
    }

    view_dispatcher_run(app->view_dispatcher);

    hack_the_planet_app_free(app);
    return 0;
}
