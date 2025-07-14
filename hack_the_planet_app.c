#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification_messages.h>

// Define missing constants
#define BUFFER_SIZE 32

// Define missing enums and types
typedef enum {
    AmplifierModeOff,
    AmplifierModeLow,
    AmplifierModeHigh
} AmplifierMode;

typedef struct {
    size_t current_record;
    bool is_bat_mode;
    bool transmitting;
    uint32_t last_update;
} AppState;

// Enhanced data structures for biological recordings
typedef struct {
    float amplitude;
    uint32_t freq_peak;
    float snr_db;
    bool signal_valid;
    bool electrode_connected; // for plant data
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

// GPIO pin definitions
#define GPIO_PIN_TRIGGER    &gpio_ext_pa7
#define GPIO_PIN_DATA_OUT   &gpio_ext_pa6
#define GPIO_PIN_STATUS_LED &gpio_ext_pa4

// Initialize GPIO pins
static void gpio_init(void) {
    furi_hal_gpio_init_simple(GPIO_PIN_TRIGGER, GpioModeOutputPushPull);
    furi_hal_gpio_init_simple(GPIO_PIN_DATA_OUT, GpioModeOutputPushPull);
    furi_hal_gpio_init_simple(GPIO_PIN_STATUS_LED, GpioModeOutputPushPull);

    // Set initial states
    furi_hal_gpio_write(GPIO_PIN_TRIGGER, false);
    furi_hal_gpio_write(GPIO_PIN_DATA_OUT, false);
    furi_hal_gpio_write(GPIO_PIN_STATUS_LED, false);
}

// Convert frequency to GPIO pulse timing (microseconds)
static uint32_t freq_to_pulse_us(uint32_t freq_hz) {
    if(freq_hz == 0) return 1000; // Default 1ms pulse
    return (1000000 / (freq_hz * 2)); // Half period in microseconds
}

// Transmit signal data via GPIO
static void transmit_signal(const BioRecord* record) {
    if(!record->signal.signal_valid) return;

    // Calculate pulse timing based on frequency
    uint32_t pulse_us = freq_to_pulse_us(record->signal.freq_peak);

    // Status LED on during transmission
    furi_hal_gpio_write(GPIO_PIN_STATUS_LED, true);

    // Send amplitude as PWM-like signal
    uint32_t amplitude_pulses = (uint32_t)(record->signal.amplitude * 100);

    for(uint32_t i = 0; i < amplitude_pulses; i++) {
        furi_hal_gpio_write(GPIO_PIN_DATA_OUT, true);
        furi_delay_us(pulse_us);
        furi_hal_gpio_write(GPIO_PIN_DATA_OUT, false);
        furi_delay_us(pulse_us);
    }

    // Trigger pulse to indicate end of transmission
    furi_hal_gpio_write(GPIO_PIN_TRIGGER, true);
    furi_delay_us(100);
    furi_hal_gpio_write(GPIO_PIN_TRIGGER, false);

    furi_hal_gpio_write(GPIO_PIN_STATUS_LED, false);
}

// Draw callback for GUI
static void draw_callback(Canvas* canvas, void* context) {
    AppState* app_state = context;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);

    const BioRecord* records = app_state->is_bat_mode ? bat_records : plant_records;
    size_t record_count = app_state->is_bat_mode ? 2 : 2;

    canvas_draw_str(canvas, 2, 12, app_state->is_bat_mode ? "BAT MODE" : "PLANT MODE");

    if(app_state->current_record < record_count) {
        const BioRecord* record = &records[app_state->current_record];

        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 24, record->id);
        canvas_draw_str(canvas, 2, 36, record->description);

        // Signal info
        char signal_info[64];
        snprintf(signal_info, sizeof(signal_info), "Freq: %lu Hz", record->signal.freq_peak);
        canvas_draw_str(canvas, 2, 48, signal_info);

        snprintf(signal_info, sizeof(signal_info), "Amp: %.2f", (double)record->signal.amplitude);
        canvas_draw_str(canvas, 2, 60, signal_info);

        if(record->signal.electrode_connected) {
            canvas_draw_str(canvas, 90, 60, "ELECTRODE OK");
        }

        if(app_state->transmitting) {
            canvas_draw_str(canvas, 90, 12, "TRANSMITTING");
        }
    }

    canvas_draw_str(canvas, 2, 64, "OK: Send | Back: Exit | Up/Down: Nav");
}

// Input callback
static void input_callback(InputEvent* input_event, void* context) {
    FuriMessageQueue* event_queue = context;
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

// Main application entry point
int32_t hack_the_planet_app(void* p) {
    UNUSED(p);

    AppState app_state = {
        .current_record = 0, .is_bat_mode = true, .transmitting = false, .last_update = 0};

    // Initialize GPIO
    gpio_init();

    // Create GUI
    Gui* gui = furi_record_open(RECORD_GUI);
    ViewPort* view_port = view_port_alloc();

    // Create message queue for input events
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    view_port_draw_callback_set(view_port, draw_callback, &app_state);
    view_port_input_callback_set(view_port, input_callback, event_queue);

    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    // Main loop
    InputEvent event;
    bool running = true;

    while(running) {
        if(furi_message_queue_get(event_queue, &event, 100) == FuriStatusOk) {
            if(event.type == InputTypePress) {
                const BioRecord* records = app_state.is_bat_mode ? bat_records : plant_records;
                size_t record_count = app_state.is_bat_mode ? 2 : 2;

                switch(event.key) {
                case InputKeyBack:
                    running = false;
                    break;
                case InputKeyOk:
                    if(app_state.current_record < record_count) {
                        app_state.transmitting = true;
                        view_port_update(view_port);
                        transmit_signal(&records[app_state.current_record]);
                        app_state.transmitting = false;
                    }
                    break;
                case InputKeyUp:
                    if(app_state.current_record > 0) {
                        app_state.current_record--;
                    }
                    break;
                case InputKeyDown:
                    if(app_state.current_record < record_count - 1) {
                        app_state.current_record++;
                    }
                    break;
                case InputKeyLeft:
                    app_state.is_bat_mode = !app_state.is_bat_mode;
                    app_state.current_record = 0;
                    break;
                case InputKeyRight:
                    app_state.is_bat_mode = !app_state.is_bat_mode;
                    app_state.current_record = 0;
                    break;
                default:
                    // Handle InputKeyMAX and other cases
                    break;
                }
            }
        }
        view_port_update(view_port);
    }

    // Cleanup
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    furi_record_close(RECORD_GUI);

    return 0;
}
