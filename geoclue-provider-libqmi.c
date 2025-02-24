#include <geoclue/gc-iface-position.h>
#include <geoclue/gc-iface-velocity.h>
#include <geoclue/gc-provider.h>
#include <libqmi-glib.h>
#include <stdint.h>

typedef struct {
  GcProvider parent;

  QrtrBus* bus;
  QmiDevice* device;
  QmiClientLoc* qmi_client_loc;

  GeoclueStatus last_status;
  GeocluePositionFields last_pos_fields;
  GeoclueAccuracy* last_accuracy;
  GeoclueVelocityFields last_velo_fields;

  guint location_report_id;

  gdouble last_lat;
  gdouble last_lon;

  gfloat last_hspeed;
  gfloat last_vspeed;

  gfloat last_altitude;

  gfloat last_direction;

  gfloat last_huncertainty;
  gfloat last_vuncertainty;

  guint64 last_time;

  GMainLoop* loop;
} GeoclueQmi;

typedef struct {
  GcProviderClass parent_class;
} GeoclueQmiClass;

#define GEOCLUE_TYPE_QMI (geoclue_qmi_get_type())
#define GEOCLUE_QMI(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GEOCLUE_TYPE_QMI, GeoclueQmi))

static void geoclue_qmi_position_init(GcIfacePositionClass* iface);
static void geoclue_qmi_velocity_init(GcIfaceVelocityClass* iface);

G_DEFINE_TYPE_WITH_CODE(GeoclueQmi, geoclue_qmi, GC_TYPE_PROVIDER,
                        G_IMPLEMENT_INTERFACE(GC_TYPE_IFACE_POSITION,
                                              geoclue_qmi_position_init)
                            G_IMPLEMENT_INTERFACE(GC_TYPE_IFACE_VELOCITY,
                                                  geoclue_qmi_velocity_init))

static gboolean get_status(GcIfaceGeoclue* gc, GeoclueStatus* status,
                           GError** error) {
  GeoclueQmi* qmi = GEOCLUE_QMI(gc);

  *status = qmi->last_status;
  return TRUE;
}

static gboolean get_velocity(GcIfaceVelocity* gc, GeoclueVelocityFields* fields,
                             int* timestamp, double* speed, double* direction,
                             double* climb, GError** error) {
  g_debug("Getting velocity");
  GeoclueQmi* qmi = GEOCLUE_QMI(gc);

  *fields = qmi->last_velo_fields;
  *timestamp = (int)qmi->last_time;
  *speed = (double)qmi->last_hspeed;
  *direction = (double)qmi->last_direction;
  *climb = (double)qmi->last_vspeed;

  return TRUE;
}

static gboolean get_position(GcIfacePosition* gc, GeocluePositionFields* fields,
                             int* timestamp, double* latitude,
                             double* longitude, double* altitude,
                             GeoclueAccuracy** accuracy, GError** error) {
  g_debug("Getting position");
  GeoclueQmi* qmi = GEOCLUE_QMI(gc);

  *fields = qmi->last_pos_fields;
  *timestamp = (int)qmi->last_time;
  *latitude = (double)qmi->last_lat;
  *longitude = (double)qmi->last_lon;
  *altitude = (double)qmi->last_altitude;
  *accuracy = geoclue_accuracy_copy(qmi->last_accuracy);

  return TRUE;
}

static gboolean set_options(GcIfaceGeoclue* gc, GHashTable* options,
                            GError** error) {
  g_debug("Setting options: Not implemented");
  return TRUE;
}

static void dispose_cb(GObject* source, GAsyncResult* result,
                       gpointer user_data) {
  GeoclueQmi* qmi = GEOCLUE_QMI(qmi);
  g_main_loop_quit(qmi->loop);
}

static void shutdown(GcProvider* provider) {
  GeoclueQmi* qmi = GEOCLUE_QMI(provider);
  QmiDeviceReleaseClientFlags flags = QMI_DEVICE_RELEASE_CLIENT_FLAGS_NONE;

  g_debug("Releasing QMI LOC client");
  if (qmi->qmi_client_loc) {
    flags |= QMI_DEVICE_RELEASE_CLIENT_FLAGS_RELEASE_CID;
    qmi_device_release_client(qmi->device, QMI_CLIENT(qmi->qmi_client_loc),
                              flags, 10, NULL, (GAsyncReadyCallback)dispose_cb,
                              qmi);
  }
}

static void geoclue_qmi_set_status(GeoclueQmi* qmi, GeoclueStatus status) {
  if (status != qmi->last_status) {
    qmi->last_status = status;

    if (status != GEOCLUE_STATUS_AVAILABLE) {
      qmi->last_pos_fields = GEOCLUE_POSITION_FIELDS_NONE;
      qmi->last_velo_fields = GEOCLUE_VELOCITY_FIELDS_NONE;
    }
    gc_iface_geoclue_emit_status_changed(GC_IFACE_GEOCLUE(qmi), status);
  }
}

void on_position_report(QmiClientLoc* client,
                        QmiIndicationLocPositionReportOutput* output,
                        gpointer user_data) {
  GeoclueQmi* qmi = GEOCLUE_QMI(user_data);
  g_debug("Got position report");
  qmi->last_velo_fields = GEOCLUE_VELOCITY_FIELDS_NONE;
  qmi->last_pos_fields = GEOCLUE_POSITION_FIELDS_NONE;

  if (qmi_indication_loc_position_report_output_get_horizontal_speed(
          output, &qmi->last_hspeed, NULL)) {
    g_debug("hspeed found");
    qmi->last_velo_fields |= GEOCLUE_VELOCITY_FIELDS_SPEED;
  }

  if (qmi_indication_loc_position_report_output_get_vertical_speed(
          output, &qmi->last_vspeed, NULL)) {
    g_debug("vspeed found");
    qmi->last_velo_fields |= GEOCLUE_VELOCITY_FIELDS_CLIMB;
  }

  if (qmi_indication_loc_position_report_output_get_heading(
          output, &qmi->last_direction, NULL)) {
    g_debug("heading found");
    qmi->last_velo_fields |= GEOCLUE_VELOCITY_FIELDS_DIRECTION;
  }

  if (qmi_indication_loc_position_report_output_get_latitude(
          output, &qmi->last_lat, NULL)) {
    g_debug("latitude found");
    qmi->last_pos_fields |= GEOCLUE_POSITION_FIELDS_LATITUDE;
  }

  if (qmi_indication_loc_position_report_output_get_longitude(
          output, &qmi->last_lon, NULL)) {
    g_debug("longitude found");
    qmi->last_pos_fields |= GEOCLUE_POSITION_FIELDS_LONGITUDE;
  }

  if (qmi_indication_loc_position_report_output_get_altitude_from_sealevel(
          output, &qmi->last_altitude, NULL)) {
    g_debug("altitude found");
    qmi->last_pos_fields |= GEOCLUE_POSITION_FIELDS_ALTITUDE;
  }

  qmi_indication_loc_position_report_output_get_horizontal_uncertainty_circular(
      output, &qmi->last_huncertainty, NULL);
  qmi_indication_loc_position_report_output_get_vertical_uncertainty(
      output, &qmi->last_vuncertainty, NULL);

  qmi_indication_loc_position_report_output_get_utc_timestamp(
      output, &qmi->last_time, NULL);

  if (qmi->last_pos_fields != GEOCLUE_POSITION_FIELDS_NONE) {
    geoclue_accuracy_set_details(
        qmi->last_accuracy, GEOCLUE_ACCURACY_LEVEL_DETAILED,
        (double)qmi->last_huncertainty, (double)qmi->last_vuncertainty);

    gc_iface_position_emit_position_changed(
        GC_IFACE_POSITION(qmi), qmi->last_pos_fields, (int)qmi->last_time,
        (double)qmi->last_lat, (double)qmi->last_lon,
        (double)qmi->last_altitude, qmi->last_accuracy);

    g_debug("Pos: %f %f", qmi->last_lat, qmi->last_lon);
  }
  if (qmi->last_velo_fields != GEOCLUE_VELOCITY_FIELDS_NONE) {
    gc_iface_velocity_emit_velocity_changed(
        GC_IFACE_VELOCITY(qmi), qmi->last_velo_fields, (int)qmi->last_time,
        (double)qmi->last_hspeed, (double)qmi->last_direction,
        (double)qmi->last_vspeed);
    g_debug("Updated velocity");
  }
}

static void loc_start_ready(QmiClientLoc* client, GAsyncResult* result,
                            gpointer user_data) {
  GError* error = NULL;
  GeoclueQmi* qmi = NULL;
  QmiMessageLocStartOutput* start_out = NULL;

  qmi = GEOCLUE_QMI(user_data);

  start_out = qmi_client_loc_start_finish(qmi->qmi_client_loc, result, &error);
  if (error) {
    g_error("Error while starting location");
    geoclue_qmi_set_status(qmi, GEOCLUE_STATUS_ERROR);
    return;
  }
  g_debug("QMI provider is now available");
  geoclue_qmi_set_status(qmi, GEOCLUE_STATUS_AVAILABLE);
}

static void register_events_ready(QmiClientLoc* client, GAsyncResult* result,
                                  gpointer user_data) {
  GError* error = NULL;
  GeoclueQmi* qmi = NULL;
  QmiMessageLocRegisterEventsOutput* events_out = NULL;
  QmiMessageLocStartInput* start_inp = NULL;
  qmi = GEOCLUE_QMI(user_data);

  events_out = qmi_client_loc_register_events_finish(qmi->qmi_client_loc,
                                                     result, &error);
  if (error) {
    g_error("Registering events failed");
    geoclue_qmi_set_status(qmi, GEOCLUE_STATUS_ERROR);
    return;
  }
  g_debug("Registered events successfully");

  start_inp = qmi_message_loc_start_input_new();

  qmi_message_loc_start_input_set_session_id(start_inp, 2, &error);

  qmi_message_loc_start_input_set_intermediate_report_state(
      start_inp, QMI_LOC_INTERMEDIATE_REPORT_STATE_ENABLE, &error);

  qmi_message_loc_start_input_set_minimum_interval_between_position_reports(
      start_inp, 1000, &error);

  qmi_message_loc_start_input_set_fix_recurrence_type(
      start_inp, QMI_LOC_FIX_RECURRENCE_TYPE_REQUEST_PERIODIC_FIXES, &error);

  if (error) {
    g_error("Error occured while creating message");
    geoclue_qmi_set_status(qmi, GEOCLUE_STATUS_ERROR);
    return;
  }

  qmi_client_loc_start(qmi->qmi_client_loc, start_inp, 15, NULL,
                       (GAsyncReadyCallback)loc_start_ready, qmi);
}

static void allocate_client_ready(QmiDevice* device, GAsyncResult* result,
                                  gpointer user_data) {
  GError* error = NULL;
  GeoclueQmi* qmi = NULL;
  QmiMessageLocRegisterEventsInput* events_inp = NULL;

  qmi = GEOCLUE_QMI(user_data);

  qmi->qmi_client_loc =
      QMI_CLIENT_LOC(qmi_device_allocate_client_finish(device, result, &error));

  if (error) {
    geoclue_qmi_set_status(qmi, GEOCLUE_STATUS_ERROR);
    return;
  }

  g_debug("Connecting QMI Loc client signals");

  qmi->location_report_id =
      g_signal_connect(qmi->qmi_client_loc, "position-report",
                       G_CALLBACK(on_position_report), qmi);

  g_debug("Creating register events input msg");

  events_inp = qmi_message_loc_register_events_input_new();
  qmi_message_loc_register_events_input_set_event_registration_mask(
      events_inp, QMI_LOC_EVENT_REGISTRATION_FLAG_POSITION_REPORT, &error);
  if (error) {
    g_warning("Couldn't set events registration");
    geoclue_qmi_set_status(qmi, GEOCLUE_STATUS_ERROR);
    return;
  }

  g_debug("Created register events input msg");
  g_debug("Registering events");

  qmi_client_loc_register_events(qmi->qmi_client_loc, events_inp, 15, NULL,
                                 (GAsyncReadyCallback)register_events_ready,
                                 qmi);
  g_debug("Registered events");
}

static void device_open_ready(QmiDevice* device, GAsyncResult* result,
                              gpointer user_data) {
  GError* error = NULL;
  GeoclueQmi* qmi = NULL;

  qmi = GEOCLUE_QMI(user_data);

  qmi_device_open_finish(device, result, &error);
  if (error) {
    geoclue_qmi_set_status(qmi, GEOCLUE_STATUS_ERROR);
    return;
  }

  g_debug("QMI device at '%s' ready", qmi_device_get_path_display(device));

  /* QMI device opened, allocate client */
  qmi_device_allocate_client(device, QMI_SERVICE_LOC, QMI_CID_NONE, 10, NULL,
                             (GAsyncReadyCallback)allocate_client_ready, qmi);
}

static void device_new_ready(GObject* source, GAsyncResult* res,
                             gpointer user_data) {
  QmiDeviceOpenFlags open_flags = QMI_DEVICE_OPEN_FLAGS_NONE;
  GError* error = NULL;
  GeoclueQmi* qmi = NULL;

  qmi = GEOCLUE_QMI(user_data);

  qmi->device = qmi_device_new_finish(res, &error);
  if (error) {
    geoclue_qmi_set_status(qmi, GEOCLUE_STATUS_ERROR);
    return;
  }

  open_flags |= QMI_DEVICE_OPEN_FLAGS_AUTO;
  open_flags |= QMI_DEVICE_OPEN_FLAGS_EXPECT_INDICATIONS;

  g_debug("QMI device ready");

  /* QMI device created, open device */
  qmi_device_open(qmi->device, open_flags, 15, NULL,
                  (GAsyncReadyCallback)device_open_ready, qmi);
}

static void bus_new_ready(GObject* source, GAsyncResult* res,
                          gpointer user_data) {
  GError* error = NULL;
  QrtrNode* node = NULL;
  GTask* task = NULL;
  gboolean found = FALSE;

  GeoclueQmi* qmi = GEOCLUE_QMI(user_data);

  qmi->bus = qrtr_bus_new_finish(res, &error);
  if (error) {
    g_warning(
        "QRTR bus unavailable. Make sure access to AF_QIPCRTR address "
        "family is granted.");
    geoclue_qmi_set_status(qmi, GEOCLUE_STATUS_ERROR);
    return;
  }

  /* Find QRTR node for LOC service */
  for (GList* l = qrtr_bus_peek_nodes(qmi->bus); l != NULL; l = l->next) {
    node = l->data;
    if (node && qrtr_node_lookup_port(node, QMI_SERVICE_LOC) >= 0) {
      found = TRUE;
      break;
    }
  }

  if (!found) {
    g_error("Service LOC not found");
    geoclue_qmi_set_status(qmi, GEOCLUE_STATUS_ERROR);
    return;
  }

  g_debug("QRTR node discovered for LOC service");

  /* QRTR node ready, create QMI device */
  qmi_device_new_from_node(node, NULL, (GAsyncReadyCallback)device_new_ready,
                           qmi);
}

static void geoclue_qmi_init(GeoclueQmi* qmi) {
  qmi->last_accuracy = geoclue_accuracy_new(GEOCLUE_ACCURACY_LEVEL_NONE, 0, 0);
  qmi->last_velo_fields = GEOCLUE_VELOCITY_FIELDS_NONE;
  qmi->last_pos_fields = GEOCLUE_POSITION_FIELDS_NONE;
  g_debug("GeoclueQmi init");

  gc_provider_set_details(
      GC_PROVIDER(qmi), "org.freedesktop.Geoclue.Providers.Qmi",
      "/org/freedesktop/Geoclue/Providers/Qmi", "Qmi", "Qmi Loc provider");
  geoclue_qmi_set_status(qmi, GEOCLUE_STATUS_ACQUIRING);

  g_debug("GeoclueQmi creating qrtr bus");
  qrtr_bus_new(1000, /* ms */
               NULL, (GAsyncReadyCallback)bus_new_ready, qmi);
}

static void geoclue_qmi_position_init(GcIfacePositionClass* iface) {
  g_debug("GeoclueQmi position init");
  iface->get_position = get_position;
}

static void geoclue_qmi_velocity_init(GcIfaceVelocityClass* iface) {
  g_debug("GeoclueQmi velocity init");
  iface->get_velocity = get_velocity;
}

static void geoclue_qmi_class_init(GeoclueQmiClass* klass) {
  g_debug("GeoclueQmi class init");
  GcProviderClass* p_class = (GcProviderClass*)klass;

  p_class->get_status = get_status;
  p_class->set_options = set_options;
  p_class->shutdown = shutdown;
}

int main(int argc, char** argv) {
  GeoclueQmi* qmi;
  g_debug("GeoclueQmi starting");
  qmi = g_object_new(GEOCLUE_TYPE_QMI, NULL);

  qmi->loop = g_main_loop_new(NULL, TRUE);

  g_debug("GeoclueQmi running main loop");
  g_main_loop_run(qmi->loop);
  g_object_unref(qmi);
  return 0;
}
