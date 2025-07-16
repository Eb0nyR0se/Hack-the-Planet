#include <furi_hal_adc.h>
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <furi/core/log.h>
#include <string.h>
#include <furi_hal_gpio.h>
#include <furi_hal_speaker.h>

// Constants
#define BUFFER_SIZE                 128
#define CALIBRATION_SAMPLES         50
#define SAMPLE_RATE_MS              50
#define REFERENCE_VOLTAGE           3.3f
#define MAX_ADC_VALUE               4095.0f
#define VOLTAGE_THRESHOLD_DIRECT    0.01f
#define VOLTAGE_THRESHOLD_AMPLIFIED 0.005f
#define MAX_DYNAMIC_RECORDS         10

// Hardware pins
#define REFERENCE_PIN        &gpio_ext_pb2
#define AMPLIFIER_OUTPUT_PIN &gpio_ext_pa6
#define ADC_CHANNEL_PA7      FuriHalAdcChannel7
#define ADC_CHANNEL_PA4      FuriHalAdcChannel4
#define ADC_CHANNEL_PB1      FuriHalAdcChannel9
#define GPIO_PIN_TRIGGER     &gpio_ext_pa7
#define GPIO_PIN_DATA_OUT    &gpio_ext_pa6
#define GPIO_PIN_STATUS_LED  &gpio_ext_pa4

// Enums
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

// Data structures
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

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    View* main_view;
    View* about_view;
    View* settings_view;
    FuriTimer* monitor_timer;
    FuriHalAdcHandle* adc_handle;
    FuriMutex* mutex; // Added for thread safety

    // Moved buffer to heap allocation
    float* voltage_buffer;

    size_t current_record;
    bool is_bat_mode;
    bool transmitting;
    uint32_t last_update;
    AmplifierMode amplifier_mode;
    AppState app_state;
    uint32_t detection_start_time;
    float amplifier_offset;
    bool calibration_complete;
    uint8_t calibration_count;
    float calibration_sum;
    uint8_t buffer_index;
    float baseline_voltage;
    float current_voltage;
    float sensitivity;
    uint32_t sample_count;
    float voltage_threshold;
    bool adc_error;
    bool app_initialized;
    bool cleanup_in_progress; // Added to prevent race conditions

    // Reduced string sizes to save stack space
    char error_message[32];
    char status_text[32];
    char voltage_text[16];
    char frequency_text[16];
    char mode_text[16];
} HackThePlanetApp;

// Forward declarations
static void hack_the_planet_timer_callback(void* context);
static void hack_the_planet_submenu_callback(void* context, uint32_t index);
static void hack_the_planet_main_draw_callback(Canvas* canvas, void* context);
static bool hack_the_planet_main_input_callback(InputEvent* event, void* context);
static void hack_the_planet_about_draw_callback(Canvas* canvas, void* context);
static bool hack_the_planet_about_input_callback(InputEvent* event, void* context);
static void hack_the_planet_settings_draw_callback(Canvas* canvas, void* context);
static bool hack_the_planet_settings_input_callback(InputEvent* event, void* context);
static AmplifierMode detect_hardware(HackThePlanetApp* app);
static void play_tone(uint32_t duration_ms);
static void hack_the_planet_app_free(HackThePlanetApp* app);

// Embedded bat echolocation data
static const BioRecord bat_records[] = {
    {.id = "bat001",
     .image_path = "assets/bat_echo_01.png",
     .audio_path = "data/bat001.wav",
     .lat = 36.7783,
     .lon = -119.4179,
     .description = "High-frequency bat echolocation recorded near cave entrance.",
     .timestamp = 1720383215,
     .signal =
         {.amplitude = 0.82,
          .freq_peak = 38450,
          .snr_db = 27.3,
          .signal_valid = true,
          .electrode_connected = false},
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
     .signal =
         {.amplitude = 0.21,
          .freq_peak = 40500,
          .snr_db = 12.7,
          .signal_valid = true,
          .electrode_connected = false},
     .suggested_species = "",
     .verified_species = "",
     .verified = false}};

// Embedded plant bioelectric data
static const BioRecord plant_records[] = {
    {.id = "plant001",
     .image_path = "assets/monstera01.jpg",
     .audio_path = "data/plant001.wav",
     .lat = 42.351,
     .lon = -71.047,
     .description = "Healthy Monstera in shade",
     .timestamp = 1720382212,
     .signal =
         {.amplitude = 0.003,
          .freq_peak = 0.12,
          .snr_db = 19.5,
          .signal_valid = true,
          .electrode_connected = true},
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
     .signal =
         {.amplitude = 0.45,
          .freq_peak = 190.1,
          .snr_db = 0,
          .signal_valid = false,
          .electrode_connected = false},
     .suggested_species = "",
     .verified_species = "",
     .verified = false}};

// GPIO initialization
static bool gpio_init(void) {
    furi_hal_gpio_init_simple(GPIO_PIN_TRIGGER, GpioModeOutputPushPull);
    furi_hal_gpio_init_simple(GPIO_PIN_DATA_OUT, GpioModeOutputPushPull);
    furi_hal_gpio_init_simple(GPIO_PIN_STATUS_LED, GpioModeOutputPushPull);

    furi_hal_gpio_write(GPIO_PIN_TRIGGER, false);
    furi_hal_gpio_write(GPIO_PIN_DATA_OUT, false);
    furi_hal_gpio_write(GPIO_PIN_STATUS_LED, false);

    return true;
}

// Frequency to pulse timing
static uint32_t freq_to_pulse_us(uint32_t freq_hz) {
    if(freq_hz == 0) return 1000;
    return (1000000 / (freq_hz * 2));
}

// Transmit signal via GPIO
static void transmit_signal(const BioRecord* record) {
    if(!record || !record->signal.signal_valid) return;

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

// Audio feedback
static void play_tone(uint32_t duration_ms) {
    if(duration_ms > 1000) duration_ms = 1000; // Limit duration
    furi_hal_speaker_start(440, 1.0f); // 440 Hz, full volume
    furi_delay_ms(duration_ms);
    furi_hal_speaker_stop();
}

// Timer callback for monitoring and calibration - FIXED
static void hack_the_planet_timer_callback(void* context) {
    HackThePlanetApp* app = (HackThePlanetApp*)context;

    // Enhanced safety checks
    if(!app || app->cleanup_in_progress) {
        return;
    }

    // Acquire mutex for thread safety
    if(furi_mutex_acquire(app->mutex, 10) != FuriStatusOk) {
        return;
    }

    // Additional safety checks after acquiring mutex
    if(!app->adc_handle || !app->voltage_buffer || !app->app_initialized) {
        furi_mutex_release(app->mutex);
        return;
    }

    // Read raw ADC value and convert to voltage
    uint16_t value = furi_hal_adc_read(app->adc_handle, FuriHalAdcChannel7);
    float voltage = ((float)value / MAX_ADC_VALUE) * REFERENCE_VOLTAGE;
    app->current_voltage = voltage;

    // Safer buffer management
    if(app->buffer_index >= BUFFER_SIZE) {
        app->buffer_index = 0;
    }
    app->voltage_buffer[app->buffer_index] = voltage;
    app->buffer_index++;
    app->sample_count++;

    // ADC error detection
    if((value == 0) || (voltage < 0.0f) || (voltage > REFERENCE_VOLTAGE)) {
        app->adc_error = true;
        snprintf(app->error_message, sizeof(app->error_message), "ADC Error");
        app->app_state = APP_STATE_ERROR;
        FURI_LOG_E("PlantMonitor", "ADC error detected");
        furi_mutex_release(app->mutex);
        return;
    } else {
        app->adc_error = false;
        snprintf(app->error_message, sizeof(app->error_message), "OK");
    }

    // Calibration
    if(!app->calibration_complete && app->app_state == APP_STATE_CALIBRATING) {
        app->calibration_sum += voltage;
        app->calibration_count++;
        if(app->calibration_count >= CALIBRATION_SAMPLES) {
            app->amplifier_offset = app->calibration_sum / CALIBRATION_SAMPLES;
            app->baseline_voltage = app->amplifier_offset;
            app->calibration_complete = true;
            snprintf(
                app->status_text,
                sizeof(app->status_text),
                "Cal: %.3f V",
                (double)app->amplifier_offset);
            FURI_LOG_I("PlantMonitor", "%s", app->status_text);
            app->app_state = APP_STATE_READY;
        }
    }

    // Monitoring
    if(app->app_state == APP_STATE_MONITORING || app->app_state == APP_STATE_PLANT_MODE) {
        float delta = fabsf(voltage - app->baseline_voltage);
        if(delta > app->voltage_threshold) {
            furi_hal_gpio_write(GPIO_PIN_STATUS_LED, true);
            snprintf(app->status_text, sizeof(app->status_text), "Spike: %.3f", (double)delta);
            FURI_LOG_W("PlantMonitor", "%s", app->status_text);
            play_tone(50);
        } else {
            furi_hal_gpio_write(GPIO_PIN_STATUS_LED, false);
        }
    }

    snprintf(app->voltage_text, sizeof(app->voltage_text), "%.3f V", (double)voltage);

    furi_mutex_release(app->mutex);
}

// Hardware detection
static AmplifierMode detect_hardware(HackThePlanetApp* app) {
    if(!app) return AMPLIFIER_MODE_ERROR;

    bool pa6_state = furi_hal_gpio_read(GPIO_PIN_DATA_OUT);

    if(!pa6_state) {
        app->amplifier_mode = AMPLIFIER_MODE_DETECTED;
        app->voltage_threshold = VOLTAGE_THRESHOLD_AMPLIFIED;
        app->sensitivity = 10.0f;
        snprintf(app->mode_text, sizeof(app->mode_text), "Amplified");
        return AMPLIFIER_MODE_DETECTED;
    } else {
        app->amplifier_mode = AMPLIFIER_MODE_NONE;
        app->voltage_threshold = VOLTAGE_THRESHOLD_DIRECT;
        app->sensitivity = 1.0f;
        snprintf(app->mode_text, sizeof(app->mode_text), "Direct");
        return AMPLIFIER_MODE_NONE;
    }
}

// Submenu callback
static void hack_the_planet_submenu_callback(void* context, uint32_t index) {
    HackThePlanetApp* app = context;
    if(!app || app->cleanup_in_progress) return;

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
    if(!app || app->cleanup_in_progress) return;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);

    const BioRecord* records = app->is_bat_mode ? bat_records : plant_records;
    size_t record_count = app->is_bat_mode ? sizeof(bat_records) / sizeof(bat_records[0]) :
                                             sizeof(plant_records) / sizeof(plant_records[0]);

    canvas_draw_str(canvas, 2, 12, app->is_bat_mode ? "BAT MODE" : "PLANT MODE");

    if(app->current_record < record_count) {
        const BioRecord* record = &records[app->current_record];

        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 24, record->id);
        canvas_draw_str(canvas, 2, 36, record->description);

        // Use smaller buffers for display
        char signal_info[32];
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

    canvas_draw_str(canvas, 2, 64, "OK: Send | Back: Menu");
}

// Main view input callback
static bool hack_the_planet_main_input_callback(InputEvent* event, void* context) {
    HackThePlanetApp* app = context;
    if(!app || app->cleanup_in_progress) return false;

    if(event->type == InputTypePress) {
        const BioRecord* records = app->is_bat_mode ? bat_records : plant_records;
        size_t record_count = app->is_bat_mode ? sizeof(bat_records) / sizeof(bat_records[0]) :
                                                 sizeof(plant_records) / sizeof(plant_records[0]);

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
    HackThePlanetApp* app = context;
    if(!app || app->cleanup_in_progress) return;

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
    if(!app || app->cleanup_in_progress) return false;

    if(event->type == InputTypePress && event->key == InputKeyBack) {
        view_dispatcher_switch_to_view(app->view_dispatcher, HackThePlanetViewSubmenu);
        return true;
    }
    return false;
}

// Settings view draw callback
static void hack_the_planet_settings_draw_callback(Canvas* canvas, void* context) {
    HackThePlanetApp* app = context;
    if(!app || app->cleanup_in_progress) return;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "Settings");

    canvas_set_font(canvas, FontSecondary);
    char temp_text[32];
    snprintf(temp_text, sizeof(temp_text), "Sens: %.1f", (double)app->sensitivity);
    canvas_draw_str(canvas, 2, 24, temp_text);

    snprintf(temp_text, sizeof(temp_text), "Thresh: %.3f V", (double)app->voltage_threshold);
    canvas_draw_str(canvas, 2, 36, temp_text);

    canvas_draw_str(canvas, 2, 48, "Mode:");
    canvas_draw_str(canvas, 2, 60, app->mode_text);
    canvas_draw_str(canvas, 2, 64, "Press Back to return");
}

// Settings view input callback
static bool hack_the_planet_settings_input_callback(InputEvent* event, void* context) {
    HackThePlanetApp* app = context;
    if(!app || app->cleanup_in_progress) return false;

    if(event->type == InputTypePress && event->key == InputKeyBack) {
        view_dispatcher_switch_to_view(app->view_dispatcher, HackThePlanetViewSubmenu);
        return true;
    }
    return false;
}

// Allocate and initialize app - FIXED
static HackThePlanetApp* hack_the_planet_app_alloc() {
    HackThePlanetApp* app = malloc(sizeof(HackThePlanetApp));
    if(!app) {
        FURI_LOG_E("PlantMonitor", "Failed to allocate memory for HackThePlanetApp");
        return NULL;
    }

    // Initialize all fields to zero/NULL
    memset(app, 0, sizeof(HackThePlanetApp));

    // Create mutex for thread safety
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    if(!app->mutex) {
        FURI_LOG_E("PlantMonitor", "Failed to create mutex");
        free(app);
        return NULL;
    }

    // Allocate voltage buffer on heap
    app->voltage_buffer = malloc(BUFFER_SIZE * sizeof(float));
    if(!app->voltage_buffer) {
        FURI_LOG_E("PlantMonitor", "Failed to allocate voltage buffer");
        furi_mutex_free(app->mutex);
        free(app);
        return NULL;
    }

    // Initialize basic state
    app->current_record = 0;
    app->is_bat_mode = true;
    app->transmitting = false;
    app->app_state = APP_STATE_MENU;
    app->amplifier_mode = AMPLIFIER_MODE_UNKNOWN;
    app->detection_start_time = furi_get_tick();
    app->sensitivity = 1.0f;
    app->voltage_threshold = VOLTAGE_THRESHOLD_DIRECT;
    app->app_initialized = false;
    app->cleanup_in_progress = false;

    // Initialize strings
    snprintf(app->error_message, sizeof(app->error_message), "OK");
    snprintf(app->status_text, sizeof(app->status_text), "Ready");
    snprintf(app->voltage_text, sizeof(app->voltage_text), "0.000 V");
    snprintf(app->mode_text, sizeof(app->mode_text), "Menu");

    // Initialize ADC handle
    app->adc_handle = furi_hal_adc_acquire();
    if(!app->adc_handle) {
        FURI_LOG_E("PlantMonitor", "Failed to acquire ADC handle");
        free(app->voltage_buffer);
        furi_mutex_free(app->mutex);
        free(app);
        return NULL;
    }

    // Initialize GUI
    app->gui = furi_record_open(RECORD_GUI);
    if(!app->gui) {
        FURI_LOG_E("PlantMonitor", "Failed to open GUI record");
        furi_hal_adc_release(app->adc_handle);
        free(app->voltage_buffer);
        furi_mutex_free(app->mutex);
        free(app);
        return NULL;
    }

    // Initialize view dispatcher
    app->view_dispatcher = view_dispatcher_alloc();
    if(!app->view_dispatcher) {
        FURI_LOG_E("PlantMonitor", "Failed to allocate view dispatcher");
        furi_record_close(RECORD_GUI);
        furi_hal_adc_release(app->adc_handle);
        free(app->voltage_buffer);
        furi_mutex_free(app->mutex);
        free(app);
        return NULL;
    }

    // Initialize submenu
    app->submenu = submenu_alloc();
    if(!app->submenu) {
        FURI_LOG_E("PlantMonitor", "Failed to allocate submenu");
        view_dispatcher_free(app->view_dispatcher);
        furi_record_close(RECORD_GUI);
        furi_hal_adc_release(app->adc_handle);
        free(app->voltage_buffer);
        furi_mutex_free(app->mutex);
        free(app);
        return NULL;
    }

    // Initialize views
    app->main_view = view_alloc();
    app->about_view = view_alloc();
    app->settings_view = view_alloc();

    if(!app->main_view || !app->about_view || !app->settings_view) {
        FURI_LOG_E("PlantMonitor", "Failed to allocate views");
        if(app->main_view) view_free(app->main_view);
        if(app->about_view) view_free(app->about_view);
        if(app->settings_view) view_free(app->settings_view);
        submenu_free(app->submenu);
        view_dispatcher_free(app->view_dispatcher);
        furi_record_close(RECORD_GUI);
        furi_hal_adc_release(app->adc_handle);
        free(app->voltage_buffer);
        furi_mutex_free(app->mutex);
        free(app);
        return NULL;
    }

    // Initialize timer
    app->monitor_timer =
        furi_timer_alloc(hack_the_planet_timer_callback, FuriTimerTypePeriodic, app);
    if(!app->monitor_timer) {
        FURI_LOG_E("PlantMonitor", "Failed to allocate timer");
        view_free(app->main_view);
        view_free(app->about_view);
        view_free(app->settings_view);
        submenu_free(app->submenu);
        view_dispatcher_free(app->view_dispatcher);
        furi_record_close(RECORD_GUI);
        furi_hal_adc_release(app->adc_handle);
        free(app->voltage_buffer);
        furi_mutex_free(app->mutex);
        free(app);
        return NULL;
    }

    // Setup submenu
    submenu_add_item(
        app->submenu,
        "Bat Mode",
        HackThePlanetSubmenuBatMode,
        hack_the_planet_submenu_callback,
        app);
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
    view_dispatcher_add_view(
        app->view_dispatcher, HackThePlanetViewSubmenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(app->view_dispatcher, HackThePlanetViewMain, app->main_view);
    view_dispatcher_add_view(app->view_dispatcher, HackThePlanetViewAbout, app->about_view);
    view_dispatcher_add_view(app->view_dispatcher, HackThePlanetViewSettings, app->settings_view);

    // Attach view dispatcher to GUI
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    // Start with submenu
    view_dispatcher_switch_to_view(app->view_dispatcher, HackThePlanetViewSubmenu);

    // Initialize GPIO
    if(!gpio_init()) {
        FURI_LOG_E("PlantMonitor", "GPIO initialization failed");
        hack_the_planet_app_free(app);
        return NULL;
    }

    // Detect hardware first
    app->app_state = APP_STATE_DETECTING;
    app->amplifier_mode = detect_hardware(app);
    if(app->amplifier_mode == AMPLIFIER_MODE_ERROR) {
        FURI_LOG_E("PlantMonitor", "Hardware detection failed");
        hack_the_planet_app_free(app);
        return NULL;
    }

    // Start calibration
    app->app_state = APP_STATE_CALIBRATING;

    // Mark as initialized before starting timer
    app->app_initialized = true;

    // Start timer last
    furi_timer_start(app->monitor_timer, SAMPLE_RATE_MS);

    return app;
}

// Free app resources
static void hack_the_planet_app_free(HackThePlanetApp* app) {
    if(!app) return;

    // Stop and free timer
    if(app->monitor_timer) {
        furi_timer_stop(app->monitor_timer);
        furi_timer_free(app->monitor_timer);
    }

    // Clean up view dispatcher
    if(app->view_dispatcher) {
        view_dispatcher_remove_view(app->view_dispatcher, HackThePlanetViewSubmenu);
        view_dispatcher_remove_view(app->view_dispatcher, HackThePlanetViewMain);
        view_dispatcher_remove_view(app->view_dispatcher, HackThePlanetViewAbout);
        view_dispatcher_remove_view(app->view_dispatcher, HackThePlanetViewSettings);
        view_dispatcher_free(app->view_dispatcher);
    }

    // Free views
    if(app->main_view) {
        view_free(app->main_view);
    }
    if(app->about_view) {
        view_free(app->about_view);
    }
    if(app->settings_view) {
        view_free(app->settings_view);
    }

    // Free submenu
    if(app->submenu) {
        submenu_free(app->submenu);
    }

    // Release ADC handle
    if(app->adc_handle) {
        furi_hal_adc_release(app->adc_handle);
    }

    // Close GUI record
    if(app->gui) {
        furi_record_close(RECORD_GUI);
    }

    // Free the app structure itself
    free(app);
}

// Main entry point for the application
int32_t hack_the_planet_app(void* p) {
    UNUSED(p);

    FURI_LOG_I("HackThePlanet", "Starting Hack The Planet application");

    // Allocate and initialize the app
    HackThePlanetApp* app = hack_the_planet_app_alloc();
    if(!app) {
        FURI_LOG_E("HackThePlanet", "Failed to allocate app");
        return -1;
    }

    FURI_LOG_I("HackThePlanet", "App allocated successfully, starting view dispatcher");

    // Run the view dispatcher (this will block until the app exits)
    view_dispatcher_run(app->view_dispatcher);

    FURI_LOG_I("HackThePlanet", "View dispatcher stopped, cleaning up");

    // Clean up when the app exits
    hack_the_planet_app_free(app);

    FURI_LOG_I("HackThePlanet", "Hack The Planet application stopped");

    return 0;
}
