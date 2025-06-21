#include "gps_uart.h"
#include "constants.h"

#include <furi.h>
#include <furi_hal_power.h>
#include <gui/gui.h>
#include <string.h>

#include <storage/storage.h>
#include <furi_hal_rtc.h>

#define TAG "GPX"

static File* gpx_log_file = NULL;
static int gpx_log_counter = 0;

typedef enum {
    EventTypeTick,
    EventTypeKey,
} EventType;

typedef struct {
    EventType type;
    InputEvent input;
} PluginEvent;

void init_gpx_log(void) {
    char buffer[256];
    Storage* storage = furi_record_open(RECORD_STORAGE);
    DateTime datetime;
    furi_hal_rtc_get_datetime(&datetime);

    char filename[128];
    snprintf(filename, sizeof(filename),
             "/ext/gpslog_%04d-%02d-%02d_%02d-%02d-%02d.gpx",
             datetime.year, datetime.month, datetime.day,
             datetime.hour, datetime.minute, datetime.second);

    gpx_log_file = storage_file_alloc(storage);
    if(storage_file_open(gpx_log_file, filename, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        unsigned int len = snprintf(
            buffer,
            sizeof(buffer),
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<gpx version=\"1.1\" creator=\"FlipperGPS\">\n"
            "<trk><name>GPS Log</name><trkseg>\n");
            if(len > 0 && len < sizeof(buffer)) {
                storage_file_write(gpx_log_file, buffer, len);
            }
    } else {
        FURI_LOG_E("GPS", "Failed to create log file");
        storage_file_free(gpx_log_file);
        gpx_log_file = NULL;
    }

    furi_record_close(RECORD_STORAGE);
}

void log_gpx(GpsUart* gps_uart) {
    if(!gpx_log_file || !gps_uart) return;
    if(!gps_uart->status.valid) return;

    char buffer[256];
    DateTime datetime;
    furi_hal_rtc_get_datetime(&datetime);

    unsigned int len = snprintf(
        buffer,
        sizeof(buffer),
        "<trkpt lat=\"%.8f\" lon=\"%.8f\">\n"
        "<ele>%.2f</ele>\n"
        "<time>%04d-%02d-%02dT%02d:%02d:%02dZ</time>\n"
        "</trkpt>\n",
        (double)gps_uart->status.latitude,
        (double)gps_uart->status.longitude,
        (double)gps_uart->status.altitude,
//        gps_uart->status.date_year + 2000,
//        gps_uart->status.date_month,
//        gps_uart->status.date_day,
        datetime.year,
        datetime.month,
        datetime.day,
        (int)gps_uart->status.time_hours,
        (int)gps_uart->status.time_minutes,
        (int)gps_uart->status.time_seconds);

        if(len > 0 && len < sizeof(buffer)) {
            storage_file_write(gpx_log_file, buffer, len);
        }
    
    gpx_log_counter++;
    if(gpx_log_counter >= 60) {
        if (!storage_file_sync(gpx_log_file)) {
            FURI_LOG_E(TAG, "failed to periodic flush file!");
        }
        gpx_log_counter = 0;
    }
}

void close_gpx_log(void) {
    if(gpx_log_file) {
        char buffer[256];

        unsigned int len = snprintf(
            buffer,
            sizeof(buffer), "</trkseg></trk></gpx>\n");
            if(len > 0 && len < sizeof(buffer)) {
                storage_file_write(gpx_log_file, buffer, len);
            }
        storage_file_close(gpx_log_file);
        storage_file_free(gpx_log_file);
        gpx_log_file = NULL;
    }
}

static void render_callback(Canvas* const canvas, void* context) {
    furi_assert(context);
    GpsUart* gps_uart = context;
    furi_mutex_acquire(gps_uart->mutex, FuriWaitForever);

    if(!gps_uart->changing_baudrate) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 32, 8, AlignCenter, AlignBottom, "Latitude");
        canvas_draw_str_aligned(canvas, 96, 8, AlignCenter, AlignBottom, "Longitude");
        canvas_draw_str_aligned(canvas, 21, 30, AlignCenter, AlignBottom, "Course");
        canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignBottom, "Speed");
        canvas_draw_str_aligned(canvas, 107, 30, AlignCenter, AlignBottom, "Altitude");
        canvas_draw_str_aligned(canvas, 32, 52, AlignCenter, AlignBottom, "Satellites");
        canvas_draw_str_aligned(canvas, 96, 52, AlignCenter, AlignBottom, "Last Fix");

        canvas_set_font(canvas, FontSecondary);
        char buffer[64];
        snprintf(buffer, 64, "%f", (double)gps_uart->status.latitude);
        canvas_draw_str_aligned(canvas, 32, 18, AlignCenter, AlignBottom, buffer);
        snprintf(buffer, 64, "%f", (double)gps_uart->status.longitude);
        canvas_draw_str_aligned(canvas, 96, 18, AlignCenter, AlignBottom, buffer);
        snprintf(buffer, 64, "%.1f", (double)gps_uart->status.course);
        canvas_draw_str_aligned(canvas, 21, 40, AlignCenter, AlignBottom, buffer);

        switch(gps_uart->speed_units) {
        case KPH:
            snprintf(buffer, 64, "%.2f kph", (double)(gps_uart->status.speed * KNOTS_TO_KPH));
            break;
        case MPH:
            snprintf(buffer, 64, "%.2f mph", (double)(gps_uart->status.speed * KNOTS_TO_MPH));
            break;
        case KNOTS:
        default:
            snprintf(buffer, 64, "%.2f kn", (double)gps_uart->status.speed);
            break;
        }

        canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignBottom, buffer);
        snprintf(
            buffer,
            64,
            "%.1f %c",
            (double)gps_uart->status.altitude,
            tolower(gps_uart->status.altitude_units));
        canvas_draw_str_aligned(canvas, 107, 40, AlignCenter, AlignBottom, buffer);
        snprintf(buffer, 64, "%d", gps_uart->status.satellites_tracked);
        canvas_draw_str_aligned(canvas, 32, 62, AlignCenter, AlignBottom, buffer);
        snprintf(
            buffer,
            64,
            "%02d:%02d:%02d UTC",
            gps_uart->status.time_hours,
            gps_uart->status.time_minutes,
            gps_uart->status.time_seconds);
        canvas_draw_str_aligned(canvas, 96, 62, AlignCenter, AlignBottom, buffer);
    } else {
        char buffer[64];
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignBottom, "Baudrate set to:");

        snprintf(buffer, 64, "%ld baud", gps_uart->baudrate);
        canvas_draw_str_aligned(canvas, 64, 47, AlignCenter, AlignBottom, buffer);
    }

    furi_mutex_release(gps_uart->mutex);
}

static void input_callback(InputEvent* input_event, void* context) {
    furi_assert(context);
    FuriMessageQueue* event_queue = context;

    PluginEvent event = {.type = EventTypeKey, .input = *input_event};
    furi_message_queue_put(event_queue, &event, FuriWaitForever);
}

int32_t gps_app(void* p) {
    UNUSED(p);

    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(PluginEvent));

    bool otg_was_enabled = furi_hal_power_is_otg_enabled();
    uint8_t attempts = 5;
    while(--attempts > 0) {
        if(furi_hal_power_enable_otg()) break;
    }
    if(attempts == 0) {
        if(furi_hal_power_get_usb_voltage() < 4.5f) {
            FURI_LOG_E(
                "GPS",
                "Error power otg enable. BQ2589 check otg fault = %d",
                furi_hal_power_check_otg_fault() ? 1 : 0);
        }
    }

    GpsUart* gps_uart = gps_uart_enable();

    gps_uart->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    if(!gps_uart->mutex) {
        FURI_LOG_E("GPS", "cannot create mutex\r\n");
        free(gps_uart);
        return 255;
    }

    // set system callbacks
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, render_callback, gps_uart);
    view_port_input_callback_set(view_port, input_callback, event_queue);

    // open GUI and register view_port
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    // start logging
    init_gpx_log();

    PluginEvent event;
    for(bool processing = true; processing;) {
        FuriStatus event_status = furi_message_queue_get(event_queue, &event, 100);

        furi_mutex_acquire(gps_uart->mutex, FuriWaitForever);

        if(event_status == FuriStatusOk) {
            // press events
            if(event.type == EventTypeKey) {
                if(event.input.type == InputTypeShort) {
                    switch(event.input.key) {
                    case InputKeyUp:
                    case InputKeyDown:
                    case InputKeyRight:
                    case InputKeyLeft:
                    case InputKeyBack:
                        break;
                    case InputKeyOk:
                        if(!gps_uart->backlight_on) {
                            notification_message_block(
                                gps_uart->notifications, &sequence_display_backlight_enforce_on);
                            gps_uart->backlight_on = true;
                        } else {
                            notification_message_block(
                                gps_uart->notifications, &sequence_display_backlight_enforce_auto);
                            notification_message(
                                gps_uart->notifications, &sequence_display_backlight_off);
                            gps_uart->backlight_on = false;
                        }
                        break;
                    default:
                        break;
                    }
                } else if(event.input.type == InputTypeLong) {
                    switch(event.input.key) {
                    case InputKeyUp:
                        gps_uart_deinit_thread(gps_uart);
                        const int baudrate_length =
                            sizeof(gps_baudrates) / sizeof(gps_baudrates[0]);
                        my_current_gps_baudrate++;
                        if(my_current_gps_baudrate >= baudrate_length) {
                            my_current_gps_baudrate = 0;
                        }
                        gps_uart->baudrate = gps_baudrates[my_current_gps_baudrate];

                        gps_uart_init_thread(gps_uart);
                        gps_uart->changing_baudrate = true;
                        furi_mutex_release(gps_uart->mutex);
                        view_port_update(view_port);
                        break;
                    case InputKeyRight:
                        gps_uart->speed_units++;
                        if(gps_uart->speed_units == INVALID) {
                            gps_uart->speed_units = KNOTS;
                        }
                        break;
                    case InputKeyBack:
                        processing = false;
                        break;
                    default:
                        break;
                    }
                }
            }
        }
        if(!gps_uart->changing_baudrate) {
            furi_mutex_release(gps_uart->mutex);
            log_gpx(gps_uart);
            view_port_update(view_port);
        } else {
            furi_delay_ms(1000);
            gps_uart->changing_baudrate = false;
        }
    }

    notification_message_block(gps_uart->notifications, &sequence_display_backlight_enforce_auto);
    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    furi_mutex_free(gps_uart->mutex);
    gps_uart_disable(gps_uart);

    if(furi_hal_power_is_otg_enabled() && !otg_was_enabled) {
        furi_hal_power_disable_otg();
    }

    close_gpx_log();
    return 0;
}
