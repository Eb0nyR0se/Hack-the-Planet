#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <notification/notification_messages.h>
#include <dolphin/dolphin.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <furi/core/log.h>

#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif

#define TAG "HackThePlanet"

// GPIO pin definitions
#define AMPLIFIER_OUTPUT_PIN &gpio_ext_pa7
#define REFERENCE_PIN        &gpio_ext_pb7
// NOTE: PB7 is not an ADC-capable pin, so no ADC channel needed here

#define DIRECT_INPUT_PIN_1   &gpio_ext_pa4
#define DIRECT_INPUT_PIN_2   &gpio_ext_pb1


// ADC channels
#define ADC_CHANNEL_PA7 FuriHalAdcChannel7
#define ADC_CHANNEL_PA6 FuriHalAdcChannel6
#define ADC_CHANNEL_PA4 FuriHalAdcChannel4
#define ADC_CHANNEL_PB1 FuriHalAdcChannel9

// Constants
#define SAMPLE_RATE_MS              50
#define BUFFER_SIZE                 128
#define VOLTAGE_THRESHOLD_AMPLIFIED 0.01f
#define VOLTAGE_THRESHOLD_DIRECT    0.1f
#define DETECTION_TIMEOUT_MS        2000
#define CALIBRATION_SAMPLES         20
#define MAX_ADC_VALUE               4095.0f
#define REFERENCE_VOLTAGE           3.3f

// Application and hardware state enums
typedef enum {
    AMPLIFIER_MODE_UNKNOWN,
    AMPLIFIER_MODE_DETECTED,
    AMPLIFIER_MODE_NONE,
    AMPLIFIER_MODE_ERROR
} AmplifierMode;

typedef enum {
    APP_STATE_DETECTING,
    APP_STATE_CALIBRATING,
    APP_STATE_READY,
    APP_STATE_MONITORING,
    APP_STATE_ERROR
} AppState;

typedef enum {
    HackThePlanetViewSubmenu,
    HackThePlanetViewMain,
} HackThePlanetView;

typedef enum {
    HackThePlanetSubmenuStart,
    HackThePlanetSubmenuSettings,
    HackThePlanetSubmenuAbout,
} HackThePlanetSubmenuIndex;

// Application context
typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    View* main_view;

    FuriTimer* monitor_timer;
    FuriHalAdcHandle* adc_handle;

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


// Forward declarations
static void hack_the_planet_timer_callback(void* context);
static void hack_the_planet_app_draw_callback(Canvas* canvas, void* context);
static bool hack_the_planet_app_input_callback(InputEvent* event, void* context);
static void hack_the_planet_submenu_callback(void* context, uint32_t index);
static AmplifierMode detect_amplifier_board(HackThePlanetApp* app);
static void calibrate_amplifier(HackThePlanetApp* app);
static float read_voltage_for_mode(HackThePlanetApp* app);
static void generate_plant_tone(HackThePlanetApp* app, float voltage_change);
static void hack_the_planet_app_free(HackThePlanetApp* app);

// Safe ADC reading with retries
static bool
    read_adc_with_error_check(HackThePlanetApp* app, FuriHalAdcChannel channel, uint16_t* value) {
    if(!app->adc_handle) return false;

    for(int i = 0; i < 3; i++) {
        *value = furi_hal_adc_read(app->adc_handle, channel);
        if(*value <= MAX_ADC_VALUE) {
            return true;
        }
        furi_delay_ms(1);
    }
    return false;
}

// Amplifier board detection implementation
static AmplifierMode detect_amplifier_board(HackThePlanetApp* app) {
    // Configure reference pin as input with pullup
    furi_hal_gpio_init(REFERENCE_PIN, GpioModeInput, GpioPullUp, GpioSpeedLow);
    furi_delay_ms(50);

    bool reference_pulled_low = !furi_hal_gpio_read(REFERENCE_PIN);

    // Configure amplifier output pin as analog input
    furi_hal_gpio_init(AMPLIFIER_OUTPUT_PIN, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    furi_delay_ms(50);

    // Read multiple ADC values
    float voltage_sum = 0.0f;
    int valid_readings = 0;
    for(int i = 0; i < 10; i++) {
        uint16_t adc_value;
        if(read_adc_with_error_check(app, ADC_CHANNEL_PA7, &adc_value)) {
            float voltage = (adc_value / MAX_ADC_VALUE) * REFERENCE_VOLTAGE;
            voltage_sum += voltage;
            valid_readings++;
        }
        furi_delay_ms(10);
    }

    if(valid_readings < 5) {
        snprintf(app->error_message, sizeof(app->error_message), "ADC read error");
        return AMPLIFIER_MODE_ERROR;
    }

    float average_voltage = voltage_sum / valid_readings;

    // Check for amplifier signature around 1.5-1.8 V
    bool amplifier_signature = (average_voltage > 1.5f && average_voltage < 1.8f);

    // Check voltage stability
    float voltage_variance = 0.0f;
    for(int i = 0; i < 5; i++) {
        uint16_t adc_value;
        if(read_adc_with_error_check(app, ADC_CHANNEL_PA7, &adc_value)) {
            float voltage = (adc_value / MAX_ADC_VALUE) * REFERENCE_VOLTAGE;
            voltage_variance += fabsf(voltage - average_voltage);
        }
        furi_delay_ms(10);
    }
    voltage_variance /= 5.0f;

    bool voltage_stable = (voltage_variance < 0.05f);

    if(reference_pulled_low && amplifier_signature && voltage_stable) {
        snprintf(app->mode_text, sizeof(app->mode_text), "Mode: Amplified");
        app->voltage_threshold = VOLTAGE_THRESHOLD_AMPLIFIED;
        app->sensitivity = 10.0f;
        app->amplifier_offset = average_voltage;
        return AMPLIFIER_MODE_DETECTED;
    } else {
        snprintf(app->mode_text, sizeof(app->mode_text), "Mode: Direct");
        app->voltage_threshold = VOLTAGE_THRESHOLD_DIRECT;
        app->sensitivity = 1.0f;
        app->amplifier_offset = 0.0f;
        return AMPLIFIER_MODE_NONE;
    }
}

// Amplifier calibration
static void calibrate_amplifier(HackThePlanetApp* app) {
    if(app->amplifier_mode != AMPLIFIER_MODE_DETECTED) {
        app->calibration_complete = true;
        return;
    }
    uint16_t adc_value;
    if(read_adc_with_error_check(app, ADC_CHANNEL_PA7, &adc_value)) {
        float voltage = (adc_value / MAX_ADC_VALUE) * REFERENCE_VOLTAGE;
        app->calibration_sum += voltage;
        app->calibration_count++;

        if(app->calibration_count >= CALIBRATION_SAMPLES) {
            app->amplifier_offset = app->calibration_sum / CALIBRATION_SAMPLES;
            app->calibration_complete = true;
            snprintf(
                app->status_text,
                sizeof(app->status_text),
                "Calibrated: %.3f V offset",
                (double)app->amplifier_offset);
        } else {
            snprintf(
                app->status_text,
                sizeof(app->status_text),
                "Calibrating... %d/%d",
                app->calibration_count,
                CALIBRATION_SAMPLES);
        }
    } else {
        app->adc_error = true;
        snprintf(app->error_message, sizeof(app->error_message), "Calibration ADC error");
    }
}

// Read voltage depending on mode - FIXED TO USE CORRECT ADC CHANNEL FOR PB1
static float read_voltage_for_mode(HackThePlanetApp* app) {
    float voltage = 0.0f;
    uint16_t adc_value;

    switch(app->amplifier_mode) {
    case AMPLIFIER_MODE_DETECTED:
        if(read_adc_with_error_check(app, ADC_CHANNEL_PA7, &adc_value)) {
            voltage =
                ((float)adc_value / MAX_ADC_VALUE) * REFERENCE_VOLTAGE - app->amplifier_offset;
            app->adc_error = false;
        } else {
            app->adc_error = true;
            snprintf(app->error_message, sizeof(app->error_message), "Amplifier ADC error");
        }
        break;

    case AMPLIFIER_MODE_NONE: {
        uint16_t adc1, adc2;
        bool ok1 = read_adc_with_error_check(app, ADC_CHANNEL_PA4, &adc1);
        bool ok2 =
            read_adc_with_error_check(app, ADC_CHANNEL_PB1, &adc2); // FIXED: Use PB1 channel
        if(ok1 && ok2) {
            float v1 = ((float)adc1 / MAX_ADC_VALUE) * REFERENCE_VOLTAGE;
            float v2 = ((float)adc2 / MAX_ADC_VALUE) * REFERENCE_VOLTAGE;
            voltage = v1 - v2;
            app->adc_error = false;
        } else {
            app->adc_error = true;
            snprintf(app->error_message, sizeof(app->error_message), "Direct ADC error");
        }
        break;
    }

    default:
        voltage = 0.0f;
        app->adc_error = true;
        break;
    }
    return voltage;
}

// Generate audio tone based on voltage change
static void generate_plant_tone(HackThePlanetApp* app, float voltage_change) {
    if(fabsf(voltage_change) < app->voltage_threshold || app->adc_error) return;

    float freq_mult = (app->amplifier_mode == AMPLIFIER_MODE_DETECTED) ? 500.0f : 100.0f;
    float frequency = 200.0f + voltage_change * app->sensitivity * freq_mult;

    if(frequency < 50.0f) frequency = 50.0f;
    if(frequency > 2000.0f) frequency = 2000.0f;
    if(isnan(frequency) || isinf(frequency)) frequency = 200.0f;

    furi_hal_speaker_start(frequency, 0.1f);
    furi_delay_ms(50);
    furi_hal_speaker_stop();

    snprintf(app->frequency_text, sizeof(app->frequency_text), "Freq: %.1f Hz", (double)frequency);
}

// Timer callback for state machine and sampling
static void hack_the_planet_timer_callback(void* context) {
    HackThePlanetApp* app = context;
    if(!app) return;

    switch(app->app_state) {
    case APP_STATE_DETECTING:
        if(furi_get_tick() - app->detection_start_time > DETECTION_TIMEOUT_MS) {
            app->amplifier_mode = detect_amplifier_board(app);
            if(app->amplifier_mode == AMPLIFIER_MODE_ERROR) {
                app->app_state = APP_STATE_ERROR;
            } else if(app->amplifier_mode == AMPLIFIER_MODE_DETECTED) {
                app->app_state = APP_STATE_CALIBRATING;
                app->calibration_count = 0;
                app->calibration_sum = 0.0f;
                app->calibration_complete = false;
            } else {
                app->app_state = APP_STATE_READY;
                snprintf(app->status_text, sizeof(app->status_text), "Direct mode ready");
            }
        } else {
            uint32_t elapsed = furi_get_tick() - app->detection_start_time;
            snprintf(app->status_text, sizeof(app->status_text), "Detecting... %lums", elapsed);
        }
        break;

    case APP_STATE_CALIBRATING:
        calibrate_amplifier(app);
        if(app->calibration_complete) {
            app->app_state = APP_STATE_READY;
        }
        if(app->adc_error) {
            app->app_state = APP_STATE_ERROR;
        }
        break;

    case APP_STATE_READY:
        // Wait for user to start monitoring
        break;

    case APP_STATE_MONITORING: {
        float voltage = read_voltage_for_mode(app);
        if(app->adc_error) {
            app->app_state = APP_STATE_ERROR;
            break;
        }

        app->current_voltage = voltage;

        app->voltage_buffer[app->buffer_index] = voltage;
        app->buffer_index = (app->buffer_index + 1) % BUFFER_SIZE;

        if(app->sample_count < BUFFER_SIZE) app->sample_count++;

        float sum = 0.0f;
        uint32_t samples = app->sample_count;
        for(uint32_t i = 0; i < samples; i++) {
            sum += app->voltage_buffer[i];
        }
        app->baseline_voltage = sum / samples;

        float voltage_change = voltage - app->baseline_voltage;

        generate_plant_tone(app, voltage_change);

        if(!isnan(voltage) && !isinf(voltage)) {
            float display_voltage = (app->amplifier_mode == AMPLIFIER_MODE_DETECTED) ?
                                        voltage * 1000 :
                                        voltage * 1000000;
            const char* unit = (app->amplifier_mode == AMPLIFIER_MODE_DETECTED) ? "mV" : "µV";

            snprintf(
                app->voltage_text,
                sizeof(app->voltage_text),
                "V: %.3f %s",
                (double)display_voltage,
                unit);

            float display_change = (app->amplifier_mode == AMPLIFIER_MODE_DETECTED) ?
                                       voltage_change * 1000 :
                                       voltage_change * 1000000;
            snprintf(
                app->status_text,
                sizeof(app->status_text),
                "Monitoring... Δ: %.2f %s",
                (double)display_change,
                unit);
        } else {
            snprintf(app->voltage_text, sizeof(app->voltage_text), "V: Error");
            snprintf(app->status_text, sizeof(app->status_text), "Invalid voltage reading");
        }
        break;
    }

    case APP_STATE_ERROR:
        snprintf(app->status_text, sizeof(app->status_text), "Error: %.55s", app->error_message);
        break;
    }

    if(app->main_view) {
        view_dispatcher_send_custom_event(app->view_dispatcher, 0);
    }
}

// Main view draw callback
static void hack_the_planet_app_draw_callback(Canvas* canvas, void* context) {
    HackThePlanetApp* app = context;
    if(!app) return;

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontPrimary);

    canvas_draw_str(canvas, 2, 12, "Hack the Planet");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 25, app->mode_text);
    canvas_draw_str(canvas, 2, 38, app->status_text);
    canvas_draw_str(canvas, 2, 51, app->voltage_text);
    canvas_draw_str(canvas, 2, 64, app->frequency_text);

    canvas_set_font(canvas, FontKeyboard);
    switch(app->app_state) {
    case APP_STATE_READY:
        canvas_draw_str(canvas, 2, 75, "OK: Start monitoring");
        break;
    case APP_STATE_MONITORING:
        canvas_draw_str(canvas, 2, 75, "OK: Stop monitoring");
        break;
    case APP_STATE_ERROR:
        canvas_draw_str(canvas, 2, 75, "OK: Retry detection");
        break;
    default:
        canvas_draw_str(canvas, 2, 75, "Please wait...");
        break;
    }
    canvas_draw_str(canvas, 2, 85, "Back: Menu");

    // Draw voltage bar graph
    if(app->sample_count > 0 && app->app_state != APP_STATE_ERROR) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_frame(canvas, 85, 20, 40, 50);

        if(!app->adc_error && !isnan(app->current_voltage) && !isinf(app->current_voltage)) {
            float scale = (app->amplifier_mode == AMPLIFIER_MODE_DETECTED) ? 5000.0f : 50.0f;
            float normalized = (app->current_voltage - app->baseline_voltage) * scale;
            int bar_height = (int)(normalized + 25);
            if(bar_height < 0) bar_height = 0;
            if(bar_height > 48) bar_height = 48;
            canvas_draw_box(canvas, 87, 68 - bar_height, 36, bar_height);
        } else {
            canvas_draw_str(canvas, 90, 45, "ERR");
        }

        const char* mode_indicator = (app->amplifier_mode == AMPLIFIER_MODE_DETECTED) ? "AMP" :
                                                                                        "DIR";
        canvas_set_font(canvas, FontKeyboard);
        canvas_draw_str(canvas, 90, 15, mode_indicator);
    }
}

// Main view input callback
static bool hack_the_planet_app_input_callback(InputEvent* event, void* context) {
    HackThePlanetApp* app = context;
    bool consumed = false;

    if(event->type == InputTypePress) {
        switch(event->key) {
        case InputKeyOk:
            switch(app->app_state) {
            case APP_STATE_READY:
                app->app_state = APP_STATE_MONITORING;
                app->sample_count = 0;
                app->buffer_index = 0;
                snprintf(app->status_text, sizeof(app->status_text), "Starting monitor...");
                consumed = true;
                break;
            case APP_STATE_MONITORING:
                app->app_state = APP_STATE_READY;
                snprintf(app->status_text, sizeof(app->status_text), "Monitoring stopped");
                furi_hal_speaker_stop();
                consumed = true;
                break;
            case APP_STATE_ERROR:
                app->app_state = APP_STATE_DETECTING;
                app->detection_start_time = furi_get_tick();
                app->adc_error = false;
                snprintf(app->status_text, sizeof(app->status_text), "Retrying detection...");
                consumed = true;
                break;
            default:
                break;
            }
            break;

        case InputKeyBack:
            if(app->app_state == APP_STATE_MONITORING) {
                app->app_state = APP_STATE_READY;
                furi_hal_speaker_stop();
            }
            // Allow view dispatcher to handle back key as well
            consumed = false;
            break;

        default:
            break;
        }
    }
    return consumed;
}

// Submenu callback
static void hack_the_planet_submenu_callback(void* context, uint32_t index) {
    HackThePlanetApp* app = context;
    switch(index) {
    case HackThePlanetSubmenuStart:
        view_dispatcher_switch_to_view(app->view_dispatcher, HackThePlanetViewMain);
        break;
    case HackThePlanetSubmenuSettings:
        // Placeholder for settings
        break;
    case HackThePlanetSubmenuAbout:
        // Placeholder for about
        break;
    }
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
    app->monitor_timer = NULL;
    app->adc_handle = NULL;

    // Initialize ADC handle
    app->adc_handle = furi_hal_adc_acquire();
    if(!app->adc_handle) {
        free(app);
        return NULL;
    }

    app->gui = furi_record_open(RECORD_GUI);
    if(!app->gui) {
        furi_hal_adc_release(app->adc_handle);
        free(app);
        return NULL;
    }

    app->view_dispatcher = view_dispatcher_alloc();
    if(!app->view_dispatcher) {
        furi_record_close(RECORD_GUI);
        furi_hal_adc_release(app->adc_handle);
        free(app);
        return NULL;
    }

    app->submenu = submenu_alloc();
    if(!app->submenu) {
        view_dispatcher_free(app->view_dispatcher);
        furi_record_close(RECORD_GUI);
        furi_hal_adc_release(app->adc_handle);
        free(app);
        return NULL;
    }

    app->main_view = view_alloc();
    if(!app->main_view) {
        submenu_free(app->submenu);
        view_dispatcher_free(app->view_dispatcher);
        furi_record_close(RECORD_GUI);
        furi_hal_adc_release(app->adc_handle);
        free(app);
        return NULL;
    }

    app->amplifier_mode = AMPLIFIER_MODE_UNKNOWN;
    app->app_state = APP_STATE_DETECTING;
    app->detection_start_time = furi_get_tick();

    app->amplifier_offset = 1.65f;
    app->calibration_complete = false;
    app->calibration_count = 0;
    app->calibration_sum = 0.0f;

    app->adc_error = false;
    snprintf(app->error_message, sizeof(app->error_message), "No error");

    app->sensitivity = 1.0f;
    app->sample_count = 0;
    app->buffer_index = 0;
    app->baseline_voltage = 0.0f;
    app->current_voltage = 0.0f;
    app->voltage_threshold = VOLTAGE_THRESHOLD_DIRECT;

    snprintf(app->status_text, sizeof(app->status_text), "Initializing...");
    snprintf(app->voltage_text, sizeof(app->voltage_text), "V: 0.000");
    snprintf(app->frequency_text, sizeof(app->frequency_text), "Freq: 0.0 Hz");
    snprintf(app->mode_text, sizeof(app->mode_text), "Mode: Detecting...");

    // Timer for main loop
    app->monitor_timer =
        furi_timer_alloc(hack_the_planet_timer_callback, FuriTimerTypePeriodic, app);
    if(!app->monitor_timer) {
        view_free(app->main_view);
        submenu_free(app->submenu);
        view_dispatcher_free(app->view_dispatcher);
        furi_record_close(RECORD_GUI);
        furi_hal_adc_release(app->adc_handle);
        free(app);
        return NULL;
    }

    // Setup submenu
    submenu_add_item(
        app->submenu,
        "Start Monitoring",
        HackThePlanetSubmenuStart,
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

    // Setup main view
    view_set_context(app->main_view, app);
    view_set_draw_callback(app->main_view, hack_the_planet_app_draw_callback);
    view_set_input_callback(app->main_view, hack_the_planet_app_input_callback);

    // Add views to dispatcher
    view_dispatcher_add_view(
        app->view_dispatcher, HackThePlanetViewSubmenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(app->view_dispatcher, HackThePlanetViewMain, app->main_view);

    // Attach view dispatcher to GUI (fullscreen)
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    // Start timer
    furi_timer_start(app->monitor_timer, SAMPLE_RATE_MS);

    return app;
}

// Free and cleanup
static void hack_the_planet_app_free(HackThePlanetApp* app) {
    if(!app) return;

    furi_hal_speaker_stop();

    if(app->monitor_timer) {
        furi_timer_stop(app->monitor_timer);
        furi_timer_free(app->monitor_timer);
    }

    if(app->view_dispatcher) {
        view_dispatcher_remove_view(app->view_dispatcher, HackThePlanetViewSubmenu);
        view_dispatcher_remove_view(app->view_dispatcher, HackThePlanetViewMain);
        view_dispatcher_free(app->view_dispatcher);
    }

    if(app->main_view) {
        view_free(app->main_view);
    }

    if(app->submenu) {
        submenu_free(app->submenu);
    }

    if(app->adc_handle) {
        furi_hal_adc_release(app->adc_handle);
    }

    if(app->gui) {
        furi_record_close(RECORD_GUI);
    }

    free(app);
}

// Main app entry point
int32_t hack_the_planet_app(void* p) {
    UNUSED(p);

    HackThePlanetApp* app = hack_the_planet_app_alloc();
    if(!app) return -1;

    // Start in submenu view
    view_dispatcher_switch_to_view(app->view_dispatcher, HackThePlanetViewSubmenu);

    // Run main loop (blocks here until user exits)
    view_dispatcher_run(app->view_dispatcher);

    // Cleanup
    hack_the_planet_app_free(app);

    return 0;
}
