#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <notification/notification_messages.h>
#include <dolphin/dolphin.h>

// GPIO pins for different configurations
#define AMPLIFIER_OUTPUT_PIN &gpio_ext_pa7  // Pin 2 on GPIO (from amplifier)
#define REFERENCE_PIN &gpio_ext_pa6        // Pin 3 on GPIO (reference/detection)
#define DIRECT_INPUT_PIN_1 &gpio_ext_pa4   // Pin 4 for direct plant connection
#define DIRECT_INPUT_PIN_2 &gpio_ext_pa5   // Pin 5 for direct plant connection

// ADC channel mappings for Flipper Zero GPIO pins
#define ADC_CHANNEL_PA7 FuriHalAdcChannel7   // Pin 2 (PA7)
#define ADC_CHANNEL_PA6 FuriHalAdcChannel6   // Pin 3 (PA6) 
#define ADC_CHANNEL_PA4 FuriHalAdcChannel4   // Pin 4 (PA4)
#define ADC_CHANNEL_PA5 FuriHalAdcChannel5   // Pin 5 (PA5)

#define SAMPLE_RATE_MS 50              // 20Hz sampling rate
#define BUFFER_SIZE 128
#define VOLTAGE_THRESHOLD_AMPLIFIED 0.01   // Lower threshold for amplified signals
#define VOLTAGE_THRESHOLD_DIRECT 0.1       // Higher threshold for direct signals
#define DETECTION_TIMEOUT_MS 2000          // Time to wait for amplifier detection
#define CALIBRATION_SAMPLES 20             // Number of samples for calibration
#define MAX_ADC_VALUE 4095.0f              // 12-bit ADC maximum
#define REFERENCE_VOLTAGE 3.3f             // Flipper Zero reference voltage

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

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    Widget* widget;
    NotificationApp* notifications;
    FuriTimer* monitor_timer;
    
    // Amplifier detection and mode
    AmplifierMode amplifier_mode;
    AppState app_state;
    uint32_t detection_start_time;
    
    // Calibration data
    float amplifier_offset;
    bool calibration_complete;
    uint8_t calibration_count;
    float calibration_sum;
    
    // Audio and measurement variables
    float voltage_buffer[BUFFER_SIZE];
    uint8_t buffer_index;
    float baseline_voltage;
    float current_voltage;
    float sensitivity;
    uint32_t sample_count;
    float voltage_threshold;
    
    // Error handling
    bool adc_error;
    char error_message[64];
    
    // GUI state
    char status_text[64];
    char voltage_text[32];
    char frequency_text[32];
    char mode_text[32];
} HackThePlanetApp;

typedef enum {
    HackThePlanetViewSubmenu,
    HackThePlanetViewMain,
    HackThePlanetViewSettings,
} HackThePlanetView;

typedef enum {
    HackThePlanetSubmenuStart,
    HackThePlanetSubmenuSettings,
    HackThePlanetSubmenuAbout,
} HackThePlanetSubmenuIndex;

// Function prototypes
static void hack_the_planet_app_draw_callback(Canvas* canvas, void* context);
static bool hack_the_planet_app_input_callback(InputEvent* event, void* context);
static uint32_t hack_the_planet_app_exit_callback(void* context);
static void hack_the_planet_submenu_callback(void* context, uint32_t index);
static void hack_the_planet_timer_callback(void* context);
static AmplifierMode detect_amplifier_board(HackThePlanetApp* app);
static bool read_adc_with_error_check(FuriHalAdcChannel channel, uint16_t* value);
static float read_voltage_for_mode(HackThePlanetApp* app);
static void calibrate_amplifier(HackThePlanetApp* app);

// Safe ADC reading with error checking
static bool read_adc_with_error_check(FuriHalAdcChannel channel, uint16_t* value) {
    if(!furi_hal_adc_is_ready()) {
        return false;
    }
    
    // Multiple attempts for reliability
    for(int i = 0; i < 3; i++) {
        *value = furi_hal_adc_read(&furi_hal_adc_handle, channel);
        if(*value <= MAX_ADC_VALUE) {
            return true;
        }
        furi_delay_ms(1);
    }
    
    return false;
}

// Improved amplifier board detection
static AmplifierMode detect_amplifier_board(HackThePlanetApp* app) {
    // Configure reference pin as input with pullup
    furi_hal_gpio_init(REFERENCE_PIN, GpioModeInput, GpioPullUp, GpioSpeedLow);
    furi_delay_ms(50); // Longer settling time
    
    // Check reference pin state
    bool reference_pulled_low = !furi_hal_gpio_read(REFERENCE_PIN);
    
    // Configure amplifier output pin for ADC reading
    furi_hal_gpio_init(AMPLIFIER_OUTPUT_PIN, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    furi_delay_ms(50);
    
    // Take multiple readings for stability
    float voltage_sum = 0.0f;
    int valid_readings = 0;
    
    for(int i = 0; i < 10; i++) {
        uint16_t adc_value;
        if(read_adc_with_error_check(ADC_CHANNEL_PA7, &adc_value)) {
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
    
    // More robust amplifier detection
    // Look for stable voltage around Vcc/2 (1.65V) with tight tolerance
    bool amplifier_signature = (average_voltage > 1.5f && average_voltage < 1.8f);
    
    // Additional check: measure voltage stability (amplifier should be stable)
    float voltage_variance = 0.0f;
    for(int i = 0; i < 5; i++) {
        uint16_t adc_value;
        if(read_adc_with_error_check(ADC_CHANNEL_PA7, &adc_value)) {
            float voltage = (adc_value / MAX_ADC_VALUE) * REFERENCE_VOLTAGE;
            voltage_variance += fabs(voltage - average_voltage);
        }
        furi_delay_ms(10);
    }
    voltage_variance /= 5.0f;
    
    bool voltage_stable = (voltage_variance < 0.05f); // Less than 50mV variance
    
    if(reference_pulled_low && amplifier_signature && voltage_stable) {
        snprintf(app->mode_text, sizeof(app->mode_text), "Mode: Amplified");
        app->voltage_threshold = VOLTAGE_THRESHOLD_AMPLIFIED;
        app->sensitivity = 10.0f;
        app->amplifier_offset = average_voltage; // Store actual measured offset
        return AMPLIFIER_MODE_DETECTED;
    } else {
        snprintf(app->mode_text, sizeof(app->mode_text), "Mode: Direct");
        app->voltage_threshold = VOLTAGE_THRESHOLD_DIRECT;
        app->sensitivity = 1.0f;
        app->amplifier_offset = 0.0f;
        return AMPLIFIER_MODE_NONE;
    }
}

// Amplifier calibration routine
static void calibrate_amplifier(HackThePlanetApp* app) {
    if(app->amplifier_mode != AMPLIFIER_MODE_DETECTED) {
        app->calibration_complete = true;
        return;
    }
    
    uint16_t adc_value;
    if(read_adc_with_error_check(ADC_CHANNEL_PA7, &adc_value)) {
        float voltage = (adc_value / MAX_ADC_VALUE) * REFERENCE_VOLTAGE;
        app->calibration_sum += voltage;
        app->calibration_count++;
        
        if(app->calibration_count >= CALIBRATION_SAMPLES) {
            app->amplifier_offset = app->calibration_sum / CALIBRATION_SAMPLES;
            app->calibration_complete = true;
            snprintf(app->status_text, sizeof(app->status_text), 
                     "Calibrated: %.3fV offset", app->amplifier_offset);
        } else {
            snprintf(app->status_text, sizeof(app->status_text), 
                     "Calibrating... %d/%d", app->calibration_count, CALIBRATION_SAMPLES);
        }
    } else {
        app->adc_error = true;
        snprintf(app->error_message, sizeof(app->error_message), "Calibration ADC error");
    }
}

// Improved voltage reading with error handling
static float read_voltage_for_mode(HackThePlanetApp* app) {
    float voltage = 0.0f;
    uint16_t adc_value;
    
    switch(app->amplifier_mode) {
        case AMPLIFIER_MODE_DETECTED:
            // Read from amplifier output with error checking
            if(read_adc_with_error_check(ADC_CHANNEL_PA7, &adc_value)) {
                voltage = (adc_value / MAX_ADC_VALUE) * REFERENCE_VOLTAGE;
                // Subtract measured amplifier offset
                voltage = voltage - app->amplifier_offset;
                app->adc_error = false;
            } else {
                app->adc_error = true;
                snprintf(app->error_message, sizeof(app->error_message), "Amplifier ADC error");
            }
            break;
            
        case AMPLIFIER_MODE_NONE:
            // Read differential voltage directly from plant electrodes
            uint16_t adc1, adc2;
            bool read1_ok = read_adc_with_error_check(ADC_CHANNEL_PA4, &adc1);
            bool read2_ok = read_adc_with_error_check(ADC_CHANNEL_PA5, &adc2);
            
            if(read1_ok && read2_ok) {
                float voltage1 = (adc1 / MAX_ADC_VALUE) * REFERENCE_VOLTAGE;
                float voltage2 = (adc2 / MAX_ADC_VALUE) * REFERENCE_VOLTAGE;
                voltage = voltage1 - voltage2;
                app->adc_error = false;
            } else {
                app->adc_error = true;
                snprintf(app->error_message, sizeof(app->error_message), "Direct ADC error");
            }
            break;
            
        case AMPLIFIER_MODE_ERROR:
        case AMPLIFIER_MODE_UNKNOWN:
        default:
            voltage = 0.0f;
            app->adc_error = true;
            break;
    }
    
    return voltage;
}

// Generate audio tone with bounds checking
static void generate_plant_tone(HackThePlanetApp* app, float voltage_change) {
    if(fabs(voltage_change) < app->voltage_threshold) return;
    if(app->adc_error) return; // Don't play tones if we have ADC errors
    
    // Map voltage change to frequency with bounds checking
    float frequency_multiplier = (app->amplifier_mode == AMPLIFIER_MODE_DETECTED) ? 500.0f : 100.0f;
    float frequency = 200.0f + (voltage_change * app->sensitivity * frequency_multiplier);
    
    // Strict bounds checking
    if(frequency < 50.0f) frequency = 50.0f;
    if(frequency > 2000.0f) frequency = 2000.0f;
    if(isnan(frequency) || isinf(frequency)) frequency = 200.0f; // Safety check
    
    // Play tone using Flipper's speaker
    furi_hal_speaker_start(frequency, 0.1f);
    furi_delay_ms(50);
    furi_hal_speaker_stop();
    
    // Update frequency display
    snprintf(app->frequency_text, sizeof(app->frequency_text), "Freq: %.1f Hz", frequency);
}

// Main state machine timer callback
static void hack_the_planet_timer_callback(void* context) {
    HackThePlanetApp* app = context;
    
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
                snprintf(app->status_text, sizeof(app->status_text), 
                         "Detecting... %lums", elapsed);
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
            // Just waiting for user to start monitoring
            break;
            
        case APP_STATE_MONITORING:
            {
                // Main monitoring loop
                float voltage = read_voltage_for_mode(app);
                
                if(app->adc_error) {
                    app->app_state = APP_STATE_ERROR;
                    break;
                }
                
                app->current_voltage = voltage;
                
                // Store in buffer with bounds checking
                if(app->buffer_index < BUFFER_SIZE) {
                    app->voltage_buffer[app->buffer_index] = voltage;
                    app->buffer_index = (app->buffer_index + 1) % BUFFER_SIZE;
                }
                
                // Calculate baseline (running average)
                if(app->sample_count < BUFFER_SIZE) {
                    app->sample_count++;
                }
                
                float sum = 0;
                uint8_t samples_to_average = (app->sample_count < BUFFER_SIZE) ? app->sample_count : BUFFER_SIZE;
                
                for(uint8_t i = 0; i < samples_to_average; i++) {
                    sum += app->voltage_buffer[i];
                }
                app->baseline_voltage = sum / samples_to_average;
                
                // Calculate voltage change from baseline
                float voltage_change = voltage - app->baseline_voltage;
                
                // Generate audio if significant change detected
                generate_plant_tone(app, voltage_change);
                
                // Update display with appropriate units and bounds checking
                if(!isnan(voltage) && !isinf(voltage)) {
                    float display_voltage = (app->amplifier_mode == AMPLIFIER_MODE_DETECTED) ? 
                                           voltage * 1000 : voltage * 1000000;
                    const char* unit = (app->amplifier_mode == AMPLIFIER_MODE_DETECTED) ? "mV" : "µV";
                    
                    snprintf(app->voltage_text, sizeof(app->voltage_text), "V: %.3f %s", display_voltage, unit);
                    
                    float display_change = (app->amplifier_mode == AMPLIFIER_MODE_DETECTED) ? 
                                          voltage_change * 1000 : voltage_change * 1000000;
                    
                    snprintf(app->status_text, sizeof(app->status_text), 
                             "Monitoring... Δ: %.2f %s", display_change, unit);
                } else {
                    snprintf(app->voltage_text, sizeof(app->voltage_text), "V: Error");
                    snprintf(app->status_text, sizeof(app->status_text), "Invalid voltage reading");
                }
            }
            break;
            
        case APP_STATE_ERROR:
            // Error state - show error message
            snprintf(app->status_text, sizeof(app->status_text), "Error: %s", app->error_message);
            break;
    }
}

// Draw callback for main view
static void hack_the_planet_app_draw_callback(Canvas* canvas, void* context) {
    HackThePlanetApp* app = context;
    
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontPrimary);
    
    // Title
    canvas_draw_str(canvas, 2, 12, "Hack the Planet");
    
    // Mode indicator
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 25, app->mode_text);
    
    // Status
    canvas_draw_str(canvas, 2, 38, app->status_text);
    
    // Voltage reading
    canvas_draw_str(canvas, 2, 51, app->voltage_text);
    
    // Frequency
    canvas_draw_str(canvas, 2, 64, app->frequency_text);
    
    // Instructions based on state
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
    
    // Voltage visualization with error indication
    if(app->sample_count > 0 && app->app_state != APP_STATE_ERROR) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_frame(canvas, 85, 20, 40, 50);
        
        if(!app->adc_error && !isnan(app->current_voltage) && !isinf(app->current_voltage)) {
            // Draw voltage bar
            float scale = (app->amplifier_mode == AMPLIFIER_MODE_DETECTED) ? 5000 : 50;
            float normalized = (app->current_voltage - app->baseline_voltage) * scale;
            int bar_height = (int)(normalized + 25);
            
            // Bounds checking for display
            if(bar_height < 0) bar_height = 0;
            if(bar_height > 48) bar_height = 48;
            
            canvas_draw_box(canvas, 87, 68 - bar_height, 36, bar_height);
        } else {
            // Error indication
            canvas_draw_str(canvas, 90, 45, "ERR");
        }
        
        // Mode indicator
        const char* mode_indicator = (app->amplifier_mode == AMPLIFIER_MODE_DETECTED) ? "AMP" : "DIR";
        canvas_set_font(canvas, FontKeyboard);
        canvas_draw_str(canvas, 90, 15, mode_indicator);
    }
}

// Input callback for main view
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
                        break;
                    case APP_STATE_MONITORING:
                        app->app_state = APP_STATE_READY;
                        snprintf(app->status_text, sizeof(app->status_text), "Monitoring stopped");
                        furi_hal_speaker_stop();
                        break;
                    case APP_STATE_ERROR:
                        // Retry detection
                        app->app_state = APP_STATE_DETECTING;
                        app->detection_start_time = furi_get_tick();
                        app->adc_error = false;
                        snprintf(app->status_text, sizeof(app->status_text), "Retrying detection...");
                        break;
                    default:
                        break;
                }
                consumed = true;
                break;
            case InputKeyBack:
                // Stop monitoring if active
                if(app->app_state == APP_STATE_MONITORING) {
                    app->app_state = APP_STATE_READY;
                    furi_hal_speaker_stop();
                }
                consumed = false; // Let view dispatcher handle
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
            // TODO: Implement settings view
            break;
        case HackThePlanetSubmenuAbout:
            // TODO: Implement about view
            break;
    }
}

// Exit callback
static uint32_t hack_the_planet_app_exit_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

// App initialization
static HackThePlanetApp* hack_the_planet_app_alloc() {
    HackThePlanetApp* app = malloc(sizeof(HackThePlanetApp));
    
    // Initialize GUI
    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    app->submenu = submenu_alloc();
    app->widget = widget_alloc();
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    
    // Initialize state machine
    app->amplifier_mode = AMPLIFIER_MODE_UNKNOWN;
    app->app_state = APP_STATE_DETECTING;
    app->detection_start_time = furi_get_tick();
    
    // Initialize calibration
    app->amplifier_offset = 1.65f; // Default offset
    app->calibration_complete = false;
    app->calibration_count = 0;
    app->calibration_sum = 0.0f;
    
    // Initialize error handling
    app->adc_error = false;
    snprintf(app->error_message, sizeof(app->error_message), "No error");
    
    // Initialize monitoring variables
    app->sensitivity = 1.0f;
    app->sample_count = 0;
    app->buffer_index = 0;
    app->baseline_voltage = 0.0f;
    app->current_voltage = 0.0f;
    app->voltage_threshold = VOLTAGE_THRESHOLD_DIRECT;
    
    // Initialize display strings
    snprintf(app->status_text, sizeof(app->status_text), "Initializing...");
    snprintf(app->voltage_text, sizeof(app->voltage_text), "V: 0.000");
    snprintf(app->frequency_text, sizeof(app->frequency_text), "Freq: 0.0 Hz");
    snprintf(app->mode_text, sizeof(app->mode_text), "Mode: Detecting...");
    
    // Create timer
    app->monitor_timer = furi_timer_alloc(hack_the_planet_timer_callback, FuriTimerTypePeriodic, app);
    
    // Setup submenu
    submenu_add_item(app->submenu, "Start Monitoring", HackThePlanetSubmenuStart, hack_the_planet_submenu_callback, app);
    submenu_add_item(app->submenu, "Settings", HackThePlanetSubmenuSettings, hack_the_planet_submenu_callback, app);
    submenu_add_item(app->submenu, "About", HackThePlanetSubmenuAbout, hack_the_planet_submenu_callback, app);
    
    // Setup main view
    View* main_view = view_alloc();
    view_set_context(main_view, app);
    view_set_draw_callback(main_view, hack_the_planet_app_draw_callback);
    view_set_input_callback(main_view, hack_the_planet_app_input_callback);
    
    // Setup view dispatcher
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_add_view(app->view_dispatcher, HackThePlanetViewSubmenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(app->view_dispatcher, HackThePlanetViewMain, main_view);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    
    // Start the state machine timer
    furi_timer_start(app->monitor_timer, SAMPLE_RATE_MS);
    
    return app;
}

// App cleanup
static void hack_the_planet_app_free(HackThePlanetApp* app) {
    furi_hal_speaker_stop();
    
    // Stop and free timer
    if(app->monitor_timer) {
        furi_timer_stop(app->monitor_timer);
        furi_timer_free(app->monitor_timer);
    }
    
    view_dispatcher_remove_view(app->view_dispatcher, HackThePlanetViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, HackThePlanetViewMain);
    view_dispatcher_free(app->view_dispatcher);
    
    submenu_free(app->submenu);
    widget_free(app->widget);
    
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    
    free(app);
}

// Main entry point
int32_t hack_the_planet_app(void* p) {
    UNUSED(p);
    
    HackThePlanetApp* app = hack_the_planet_app_alloc();
    
    view_dispatcher_switch_to_view(app->view_dispatcher, HackThePlanetViewSubmenu);
    view_dispatcher_run(app->view_dispatcher);
    
    hack_the_planet_app_free(app);
    
    return 0;
}
