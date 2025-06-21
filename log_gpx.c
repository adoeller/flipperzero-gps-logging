#include <storage/storage.h>
#include <furi.h>
#include <furi_hal_rtc.h>
//#include "gps_uart.h"

#define TAG "GPX"

static File* gpx_log_file = NULL;
static int gpx_log_counter = 0;

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
