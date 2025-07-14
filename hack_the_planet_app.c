#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/view.h>
#include <input/input.h>
#include <notification/notification_messages.h>

// Define missing constants
#define BUFFER_SIZE 32
#define CALIBRATION_SAMPLES 50
#define SAMPLE_RATE_MS 100
#define REFERENCE_VOLTAGE 3.3f
#define MAX_ADC_VALUE 4095.0f
#define VOLTAGE_THRESHOLD_DIRECT 0.01f
#define VOLTAGE_THRESHOLD_AMPLIFIED 0.005f

// Hardware pins
#define REFERENCE_PIN &gpio_ext_pb2
#define AMPLIFIER_OUTPUT_PIN &gpio_ext_pa7
#define ADC_CHANNEL_PA7 FuriHalAdcChannel7
#define ADC_CHANNEL_PA4 FuriHalAdcChannel4
#define ADC_CHANNEL_PB1 FuriHalAdcChannel9
#define GPIO_PIN_TRIGGER &gpio_ext_pa7
#define GPIO_PIN_DATA_OUT &gpio_ext_pa6
#define GPIO_PIN_STATUS_LED &gpio_ext_pa4

// Application and hardware state enums
typedef enum {
    AMPLIFIER_MODE_UNKNOWN,
    AMPLIFIER_MODE_DETECTED,
    AMPLIFIER_MODE_NONE,
    AMPLIFIER_MODE_ERROR
} AmplifierMode;

typedef enum {
    APP_STATE_MENU,
    APP_STATE_DETECTING,
    APP_STATE_CALIBRATING,
    APP_STATE_READY,
    APP_STATE_MONITORING,
    APP_STATE_BAT_MODE,
    APP_STATE_PLANT_MODE,
    APP_STATE_ABOUT,
    APP_STATE_SETTINGS,
    APP_STATE_ERROR
} AppState;

typedef enum {
    HackThePlanetViewSubmenu,
    HackThePlanetViewMain,
    HackThePlanetViewAbout,
    HackThePlanetViewSettings,
} HackThePlanetView;

typedef enum {
    HackThePlanetSubmenuBatMode,
    HackThePlanetSubmenuPlantMode,
    HackThePlanetSubmenuSettings,
    HackThePlanetSubmenuAbout,
} HackThePlanetSubmenuIndex;

// Enhanced data structures for biological recordings
typedef struct {
    float amplitude;
    uint32_t freq_peak;
    float snr_db;
    bool signal_valid;
    bool electrode_connected;
} SignalData;

typedef struct {
    char id[16];
    char image_path[64];
    char audio_path[64];
    float lat;
    float lon;
    char description[128];
    uint32_t timestamp;
    SignalData signal;
    char suggested_species[64];
    char verified_species[64];
    bool verified;
} BioRecord;

// Application context
typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    View* main_view;
    View* about_view;
    View* settings_view;

    FuriTimer* monitor_timer;
    FuriHalAdcHandle* adc_handle;

    // Original app state
    size_t current_record;
    bool is_bat_mode;
    bool transmitting;
    uint32_t last_update;

    // Plant monitoring state
    AmplifierMode amplifier_mode;
    AppState app_state;
    uint32_t detection_start_time;

    float amplifier_offset;
    bool calibration_complete;
    uint8_t calibration_count;
    float calibration_sum;

    float voltage_buffer[BUFFER_SIZE];
    uint8_t buffer_index;
    float baseline_voltage;
    float current_voltage;
    float sensitivity;
    uint32_t sample_count;
    float voltage_threshold;

    bool adc_error;
    char error_message[64];

    char status_text[64];
    char voltage_text[32];
    char frequency_text[32];
    char mode_text[32];
} HackThePlanetApp;

// Embedded bat echolocation data
static const BioRecord bat_records[] = {
    {.id = "bat001",
     .image_path = "assets/bat_echo_01.png",
     .audio_path = "data/bat001.wav",
     .lat = 36.7783,
     .lon = -119.4179,
     .description = "High-frequency bat echolocation recorded near cave entrance.",
     .timestamp = 1720383215,
     .signal = {.amplitude = 0.82, .freq_peak = 38450, .snr_db = 27.3, .signal_valid = true, .electrode_connected = false},
     .suggested_species = "Myotis lucifugus",
     .verified_species = "",
     .verified = false},
    {.id = "bat002",
     .image_path = "assets/unknown_bat_echo.png",
     .audio_path = "data/bat002.wav",
     .lat = 34.0522,
     .lon = -118.2437,
     .description = "Low-amplitude bat pass near urban tree line.",
     .timestamp = 1720383501,
     .signal = {.amplitude = 0.21, .freq_peak = 40500, .snr_db = 12.7, .signal_valid = true, .electrode_connected = false},
     .suggested_species = "",
     .verified_species = "",
     .verified = false}
};

// Embedded plant bioelectric data
static const BioRecord plant_records[] = {
    {.id = "plant001",
     .image_path = "assets/monstera01.jpg",
     .audio_path = "data/plant001.wav",
     .lat = 42.351,
     .lon = -71.047,
     .description = "Healthy Monstera in shade",
     .timestamp = 1720382212,
     .signal = {.amplitude = 0.003, .freq_peak = 0.12, .snr_db = 19.5, .signal_valid = true, .electrode_connected = true},
     .suggested_species = "Monstera deliciosa",
     .verified_species = "",
     .verified = false},
    {.id = "plant002",
     .image_path = "assets/unknown_leaf.jpg",
     .audio_path = "",
     .lat = 40.7128,
     .lon = -74.006,
     .description = "",
     .timestamp = 0,
     .signal = {.amplitude = 0.45, .freq_peak = 190.1, .snr_db = 0, .signal_valid = false, .electrode_connected = false},
     .suggested_species = "",
     .verified_species = "",
     .verified = false}
};

// Forward declarations
static void hack_the_planet_timer_callback(void* context);
static void hack_the_planet_submenu_callback(void* context, uint32_t index);
static void hack_the_planet_main_draw_callback(Canvas* canvas, void* context);
static bool hack_the_planet_main_input_callback(InputEvent* event, void* context);
static void hack_the_planet_about_draw_callback(Canvas* canvas, void* context);
static bool hack_the_planet_about_input_callback(InputEvent* event, void* context);
static void hack_the_planet_settings_draw_callback(Canvas* canvas, void* context);
static bool hack_the_planet_settings_input_callback(InputEvent* event, void* context);

// Initialize GPIO pins
static void gpio_init(void) {
    furi_hal_gpio_init_simple(GPIO_PIN_TRIGGER, GpioModeOutputPushPull);
    furi_hal_gpio_init_simple(GPIO_PIN_DATA_OUT, GpioModeOutputPushPull);
    furi_hal_gpio_init_simple(GPIO_PIN_STATUS_LED, GpioModeOutputPushPull);

    furi_hal_gpio_write(GPIO_PIN_TRIGGER, false);
    furi_hal_gpio_write(GPIO_PIN_DATA_OUT, false);
    furi_hal_gpio_write(GPIO_PIN_STATUS_LED, false);
}

// Convert frequency to GPIO pulse timing (microseconds)
static uint32_t freq_to_pulse_us(uint32_t freq_hz) {
    if(freq_hz == 0) return 1000;
    return (1000000 / (freq_hz * 2));
}

// Transmit signal data via GPIO
static void transmit_signal(const BioRecord* record) {
    if(!record->signal.signal_valid) return;

    uint32_t pulse_us = freq_to_pulse_us(record->signal.freq_peak);
    furi_hal_gpio_write(GPIO_PIN_STATUS_LED, true);

    uint32_t amplitude_pulses = (uint32_t)(record->signal.amplitude * 100);
    for(uint32_t i = 0; i < amplitude_pulses; i++) {
        furi_hal_gpio_write(GPIO_PIN_DATA_OUT, true);
        furi_delay_us(pulse_us);
        furi_hal_gpio_write(GPIO_PIN_DATA_OUT, false);
        furi_delay_us(pulse_us);
    }

    furi_hal_gpio_write(GPIO_PIN_TRIGGER, true);
    furi_delay_us(100);
    furi_hal_gpio_write(GPIO_PIN_TRIGGER, false);
    furi_hal_gpio_write(GPIO_PIN_STATUS_LED, false);
}

// Timer callback for plant monitoring
static void hack_the_planet_timer_callback(void* context) {
    HackThePlanetApp* app = context;
    // Add your plant monitoring logic here
    UNUSED(app);
}

// Submenu callback
static void hack_the_planet_submenu_callback(void* context, uint32_t index) {
    HackThePlanetApp* app = context;
    switch(index) {
    case HackThePlanetSubmenuBatMode:
        app->is_bat_mode = true;
        app->current_record = 0;
        app->app_state = APP_STATE_BAT_MODE;
        view_dispatcher_switch_to_view(app->view_dispatcher, HackThePlanetViewMain);
        break;
    case HackThePlanetSubmenuPlantMode:
        app->is_bat_mode = false;
        app->current_record = 0;
        app->app_state = APP_STATE_PLANT_MODE;
        view_dispatcher_switch_to_view(app->view_dispatcher, HackThePlanetViewMain);
        break;
    case HackThePlanetSubmenuSettings:
        app->app_state = APP_STATE_SETTINGS;
        view_dispatcher_switch_to_view(app->view_dispatcher, HackThePlanetViewSettings);
        break;
    case HackThePlanetSubmenuAbout:
        app->app_state = APP_STATE_ABOUT;
        view_dispatcher_switch_to_view(app->view_dispatcher, HackThePlanetViewAbout);
        break;
    }
}

// Main view draw callback
static void hack_the_planet_main_draw_callback(Canvas* canvas, void* context) {
    HackThePlanetApp* app = context;
    
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);

    const BioRecord* records = app->is_bat_mode ? bat_records : plant_records;
    size_t record_count = app->is_bat_mode ? 2 : 2;

    canvas_draw_str(canvas, 2, 12, app->is_bat_mode ? "BAT MODE" : "PLANT MODE");

    if(app->current_record < record_count) {
        const BioRecord* record = &records[app->current_record];

        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 24, record->id);
        canvas_draw_str(canvas, 2, 36, record->description);

        char signal_info[64];
        snprintf(signal_info, sizeof(signal_info), "Freq: %lu Hz", record->signal.freq_peak);
        canvas_draw_str(canvas, 2, 48, signal_info);

        snprintf(signal_info, sizeof(signal_info), "Amp: %.2f", (double)record->signal.amplitude);
        canvas_draw_str(canvas, 2, 60, signal_info);

        if(record->signal.electrode_connected) {
            canvas_draw_str(canvas, 90, 60, "ELECTRODE OK");
        }

        if(app->transmitting) {
            canvas_draw_str(canvas, 90, 12, "TRANSMITTING");
        }
    }

    canvas_draw_str(canvas, 2, 64, "OK: Send | Back: Menu | Up/Down: Nav");
}

// Main view input callback
static bool hack_the_planet_main_input_callback(InputEvent* event, void* context) {
    HackThePlanetApp* app = context;
    
    if(event->type == InputTypePress) {
        const BioRecord* records = app->is_bat_mode ? bat_records : plant_records;
        size_t record_count = app->is_bat_mode ? 2 : 2;

        switch(event->key) {
        case InputKeyBack:
            view_dispatcher_switch_to_view(app->view_dispatcher, HackThePlanetViewSubmenu);
            return true;
        case InputKeyOk:
            if(app->current_record < record_count) {
                app->transmitting = true;
                transmit_signal(&records[app->current_record]);
                app->transmitting = false;
            }
            return true;
        case InputKeyUp:
            if(app->current_record > 0) {
                app->current_record--;
            }
            return true;
        case InputKeyDown:
            if(app->current_record < record_count - 1) {
                app->current_record++;
            }
            return true;
        case InputKeyLeft:
            app->is_bat_mode = !app->is_bat_mode;
            app->current_record = 0;
            return true;
        case InputKeyRight:
            app->is_bat_mode = !app->is_bat_mode;
            app->current_record = 0;
            return true;
        default:
            break;
        }
    }
    return false;
}

// About view draw callback
static void hack_the_planet_about_draw_callback(Canvas* canvas, void* context) {
    UNUSED(context);
    
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "Hack The Planet");
    
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 24, "Bio-Signal Monitor v1.0");
    canvas_draw_str(canvas, 2, 36, "Monitors plant bioelectric");
    canvas_draw_str(canvas, 2, 48, "signals and bat echolocation");
    canvas_draw_str(canvas, 2, 60, "Press Back to return");
}

// About view input callback
static bool hack_the_planet_about_input_callback(InputEvent* event, void* context) {
    HackThePlanetApp* app = context;
    
    if(event->type == InputTypePress && event->key == InputKeyBack) {
        view_dispatcher_switch_to_view(app->view_dispatcher, HackThePlanetViewSubmenu);
        return true;
    }
    return false;
}

// Settings view draw callback
static void hack_the_planet_settings_draw_callback(Canvas* canvas, void* context) {
    HackThePlanetApp* app = context;
    
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "Settings");
    
    canvas_set_font(canvas, FontSecondary);
    
    char sensitivity_text[32];
    snprintf(sensitivity_text, sizeof(sensitivity_text), "Sensitivity: %.1f", (double)app->sensitivity);
    canvas_draw_str(canvas, 2, 24, sensitivity_text);
    
    char threshold_text[32];
    snprintf(threshold_text, sizeof(threshold_text), "Threshold: %.3f V", (double)app->voltage_threshold);
    canvas_draw_str(canvas, 2, 36, threshold_text);
    
    canvas_draw_str(canvas, 2, 48, "Amplifier Mode:");
    const char* mode_str = "Unknown";
    switch(app->amplifier_mode) {
        case AMPLIFIER_MODE_DETECTED: mode_str = "Detected"; break;
        case AMPLIFIER_MODE_NONE: mode_str = "Direct"; break;
        case AMPLIFIER_MODE_ERROR: mode_str = "Error"; break;
        default: break;
    }
    canvas_draw_str(canvas, 2, 60, mode_str);
    
    canvas_draw_str(canvas, 2, 64, "Press Back to return");
}

// Settings view input callback
static bool hack_the_planet_settings_input_callback(InputEvent* event, void* context) {
    HackThePlanetApp* app = context;
    
    if(event->type == InputTypePress && event->key == InputKeyBack) {
        view_dispatcher_switch_to_view(app->view_dispatcher, HackThePlanetViewSubmenu);
        return true;
    }
    return false;
}

// Allocate and initialize app
static HackThePlanetApp* hack_the_planet_app_alloc() {
    HackThePlanetApp* app = malloc(sizeof(HackThePlanetApp));
    if(!app) return NULL;

    // Initialize all pointers to NULL first
    app->gui = NULL;
    app->view_dispatcher = NULL;
    app->submenu = NULL;
    app->main_view = NULL;
    app->about_view = NULL;
    app->settings_view = NULL;
    app->monitor_timer = NULL;
    app->adc_handle = NULL;

    // Initialize original app state
    app->current_record = 0;
    app->is_bat_mode = true;
    app->transmitting = false;
    app->last_update = 0;

    // Initialize plant monitoring state
    app->amplifier_mode = AMPLIFIER_MODE_UNKNOWN;
    app->app_state = APP_STATE_MENU;
    app->detection_start_time = furi_get_tick();
    app->amplifier_offset = 1.65f;
    app->calibration_complete = false;
    app->calibration_count = 0;
    app->calibration_sum = 0.0f;
    app->adc_error = false;
    app->sensitivity = 1.0f;
    app->sample_count = 0;
    app->buffer_index = 0;
    app->baseline_voltage = 0.0f;
    app->current_voltage = 0.0f;
    app->voltage_threshold = VOLTAGE_THRESHOLD_DIRECT;

    snprintf(app->error_message, sizeof(app->error_message), "No error");
    snprintf(app->status_text, sizeof(app->status_text), "Ready");
    snprintf(app->voltage_text, sizeof(app->voltage_text), "V: 0.000");
    snprintf(app->frequency_text, sizeof(app->frequency_text), "Freq: 0.0 Hz");
    snprintf(app->mode_text, sizeof(app->mode_text), "Mode: Menu");

    // Initialize ADC handle
    app->adc_handle = furi_hal_adc_acquire();
    if(!app->adc_handle) {
        free(app);
        return NULL;
    }

    // Initialize GUI
    app->gui = furi_record_open(RECORD_GUI);
    if(!app->gui) {
        furi_hal_adc_release(app->adc_handle);
        free(app);
        return NULL;
    }

    // Initialize view dispatcher
    app->view_dispatcher = view_dispatcher_alloc();
    if(!app->view_dispatcher) {
        furi_record_close(RECORD_GUI);
        furi_hal_adc_release(app->adc_handle);
        free(app);
        return NULL;
    }

    // Initialize submenu
    app->submenu = submenu_alloc();
    if(!app->submenu) {
        view_dispatcher_free(app->view_dispatcher);
        furi_record_close(RECORD_GUI);
        furi_hal_adc_release(app->adc_handle);
        free(app);
        return NULL;
    }

    // Initialize views
    app->main_view = view_alloc();
    app->about_view = view_alloc();
    app->settings_view = view_alloc();
    
    if(!app->main_view || !app->about_view || !app->settings_view) {
        if(app->main_view) view_free(app->main_view);
        if(app->about_view) view_free(app->about_view);
        if(app->settings_view) view_free(app->settings_view);
        submenu_free(app->submenu);
        view_dispatcher_free(app->view_dispatcher);
        furi_record_close(RECORD_GUI);
        furi_hal_adc_release(app->adc_handle);
        free(app);
        return NULL;
    }

    // Initialize timer
    app->monitor_timer = furi_timer_alloc(hack_the_planet_timer_callback, FuriTimerTypePeriodic, app);
    if(!app->monitor_timer) {
        view_free(app->main_view);
        view_free(app->about_view);
        view_free(app->settings_view);
        submenu_free(app->submenu);
        view_dispatcher_free(app->view_dispatcher);
        furi_record_close(RECORD_GUI);
        furi_hal_adc_release(app->adc_handle);
        free(app);
        return NULL;
    }

    // Setup submenu
    submenu_add_item(app->submenu, "Bat Mode", HackThePlanetSubmenuBatMode, hack_the_planet_submenu_callback, app);
    submenu_add_item(app->submenu, "Plant Mode", HackThePlanetSubmenuPlantMode, hack_the_planet_submenu_callback, app);
    submenu_add_item(app->submenu, "Settings", HackThePlanetSubmenuSettings, hack_the_planet_submenu_callback, app);
    submenu_add_item(app->submenu, "About", HackThePlanetSubmenuAbout, hack_the_planet_submenu_callback, app);

    // Setup views
    view_set_context(app->main_view, app);
    view_set_draw_callback(app->main_view, hack_the_planet_main_draw_callback);
    view_set_input_callback(app->main_view, hack_the_planet_main_input_callback);

    view_set_context(app->about_view, app);
    view_set_draw_callback(app->about_view, hack_the_planet_about_draw_callback);
    view_set_input_callback(app->about_view, hack_the_planet_about_input_callback);

    view_set_context(app->settings_view, app);
    view_set_draw_callback(app->settings_view, hack_the_planet_settings_draw_callback);
    view_set_input_callback(app->settings_view, hack_the_planet_settings_input_callback);

    // Add views to dispatcher
    view_dispatcher_add_view(app->view_dispatcher, HackThePlanetViewSubmenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(app->view_dispatcher, HackThePlanetViewMain, app->main_view);
    view_dispatcher_add_view(app->view_dispatcher, HackThePlanetViewAbout, app->about_view);
    view_dispatcher_add_view(app->view_dispatcher, HackThePlanetViewSettings, app->settings_view);

    // Attach view dispatcher to GUI
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    // Start with submenu
    view_dispatcher_switch_to_view(app->view_dispatcher, HackThePlanetViewSubmenu);

    // Start timer
    furi_timer_start(app->monitor_timer, SAMPLE_RATE_MS);

    return app;
}

// Free app resources
static void hack_the_planet_app_free(HackThePlanetApp* app) {
    if(!app) return;

    if(app->monitor_timer) {
        furi_timer_stop(app->monitor_timer);
        furi_timer_free(app->monitor_timer);
    }

    if(app->view_dispatcher) {
        view_dispatcher_remove_view(app->view_dispatcher, HackThePlanetViewSubmenu);
        view_dispatcher_remove_view(app->view_dispatcher, HackThePlanetViewMain);
        view_dispatcher_remove_view(app->view_dispatcher, HackThePlanetViewAbout);
        view_dispatcher_remove_view(app->view_dispatcher, HackThePlanetViewSettings);
        view_dispatcher_free(app->view_dispatcher);
    }

    if(app->main_view) view_free(app->main_view);
    if(app->about_view) view_free(app->about_view);
    if(app->settings_view) view_free(app->settings_view);
    if(app->submenu) submenu_free(app->submenu);
    if(app->gui) furi_record_close(RECORD_GUI);
    if(app->adc_handle) furi_hal_adc_release(app->adc_handle);

    free(app);
}

// Main application entry point
int32_t hack_the_planet_app(void* p) {
    UNUSED(p);

    // Initialize GPIO
    gpio_init();

    // Allocate app
    HackThePlanetApp* app = hack_the_planet_app_alloc();
    if(!app) {
        return -1;
    }

    // Run view dispatcher
    view_dispatcher_run(app->view_dispatcher);

    // Cleanup
    hack_the_planet_app_free(app);

    return 0;
}
