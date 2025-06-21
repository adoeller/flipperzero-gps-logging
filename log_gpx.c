#include <storage/storage.h>
#include <datetime.h>
#include <furi_hal.h>

static void init_gpx_log(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    DateTime datetime;
    datetime_get(&datetime);

    char filename[128];
    snprintf(filename, sizeof(filename),
             "/ext/gpslog_%04d-%02d-%02d_%02d-%02d-%02d.gpx",
             datetime.year, datetime.month, datetime.day,
             datetime.hour, datetime.minute, datetime.second);

    gpx_log_file = storage_file_alloc(storage);
    if(storage_file_open(gpx_log_file, filename, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_printf(gpx_log_file,
                            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                            "<gpx version=\"1.1\" creator=\"FlipperGPS\">\n"
                            "<trk><name>GPS Log</name><trkseg>\n");
    } else {
        FURI_LOG_E("GPS", "Failed to create log file");
        storage_file_free(gpx_log_file);
        gpx_log_file = NULL;
    }

    furi_record_close(RECORD_STORAGE);
}

static void log_gpx(GpsUart* gps_uart) {
    if(!gpx_log_file || !gps_uart) return;
    if(!gps_uart->status.valid) return;

    storage_file_printf(
        gpx_log_file,
        "<trkpt lat=\"%.8f\" lon=\"%.8f\">\n"
        "<ele>%.2f</ele>\n"
        "<time>%04d-%02d-%02dT%02d:%02d:%02dZ</time>\n"
        "</trkpt>\n",
        gps_uart->status.latitude,
        gps_uart->status.longitude,
        gps_uart->status.altitude,
        gps_uart->status.date_year + 2000,
        gps_uart->status.date_month,
        gps_uart->status.date_day,
        gps_uart->status.time_hours,
        gps_uart->status.time_minutes,
        gps_uart->status.time_seconds);

    gpx_log_counter++;
    if(gpx_log_counter >= 60) {
        storage_file_sync(gpx_log_file);
        gpx_log_counter = 0;
    }
}

static void close_gpx_log(void) {
    if(gpx_log_file) {
        storage_file_printf(gpx_log_file, "</trkseg></trk></gpx>\n");
        storage_file_close(gpx_log_file);
        storage_file_free(gpx_log_file);
        gpx_log_file = NULL;
    }
}
