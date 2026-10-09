#include <stdlib.h>
#include <string.h>
#include "openthread/coap.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "esp_log.h"
#include "cJSON.h"
#include "valve_control.h"

static const char *TAG = "coap_underfloor_heating";

static otCoapResource s_res_status;
static otCoapResource s_res_temp;
static otCoapResource s_res_setpoint;

// Status aller Zonen:
// {"zones":[{"zone":1,"t":21.5,"sp":22.0,"valid":true,"open":false,"pos":0}, ...]}
static char *build_status_json(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "zones");
    zone_status_t st;
    for (size_t i = 0; valve_control_get(i, &st); i++) {
        cJSON *z = cJSON_CreateObject();
        cJSON_AddNumberToObject(z, "zone", st.zone);
        if (st.temp_valid) cJSON_AddNumberToObject(z, "t", st.temp);
        else               cJSON_AddNullToObject(z, "t");
        cJSON_AddNumberToObject(z, "sp", st.setpoint);
        cJSON_AddBoolToObject(z, "valid", st.temp_valid);
        cJSON_AddBoolToObject(z, "open", st.valve_open);   // Sollzustand
        cJSON_AddNumberToObject(z, "pos", st.pos);         // geschaetzte Position in %
        cJSON_AddItemToArray(arr, z);
    }
    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return str;   // Aufrufer: free()
}

static void send_response(otInstance *instance, otMessage *request,
                          const otMessageInfo *info, otCoapCode code,
                          const char *payload)
{
    otMessage *response = otCoapNewMessage(instance, NULL);
    if (!response) return;

    // CON -> ACK, NON -> NON
    otCoapType type = (otCoapMessageGetType(request) == OT_COAP_TYPE_CONFIRMABLE)
                          ? OT_COAP_TYPE_ACKNOWLEDGMENT
                          : OT_COAP_TYPE_NON_CONFIRMABLE;
    otCoapMessageInitResponse(response, request, type, code);

    if (payload) {
        otCoapMessageAppendContentFormatOption(response, OT_COAP_OPTION_CONTENT_FORMAT_JSON);
        otCoapMessageSetPayloadMarker(response);
        otMessageAppend(response, payload, strlen(payload));
    }

    otError err = otCoapSendResponse(instance, response, info);
    if (err != OT_ERROR_NONE) {
        ESP_LOGW(TAG, "Failed to send response: %d", err);
        otMessageFree(response);
    }
}

// Liest den Body als JSON; NULL bei Fehler
static cJSON *read_json_body(otMessage *message)
{
    uint16_t offset = otMessageGetOffset(message);
    uint16_t length = otMessageGetLength(message) - offset;
    if (length == 0 || length >= 128) return NULL;

    char body[128] = {0};
    otMessageRead(message, offset, body, length);
    return cJSON_Parse(body);
}

static bool is_write(otCoapCode c)
{
    return c == OT_COAP_CODE_PUT || c == OT_COAP_CODE_POST;
}

// ---- GET /heating ----------------------------------------------------------
static void status_handler(void *ctx, otMessage *message, const otMessageInfo *info)
{
    otInstance *instance = esp_openthread_get_instance();
    if (otCoapMessageGetCode(message) != OT_COAP_CODE_GET) {
        send_response(instance, message, info, OT_COAP_CODE_METHOD_NOT_ALLOWED, NULL);
        return;
    }
    char *json = build_status_json();
    send_response(instance, message, info, OT_COAP_CODE_CONTENT, json);
    free(json);
}

// ---- PUT /heating/temp  {"zone":1,"t":21.5} --------------------------------
static void temp_handler(void *ctx, otMessage *message, const otMessageInfo *info)
{
    otInstance *instance = esp_openthread_get_instance();
    bool is_con = otCoapMessageGetType(message) == OT_COAP_TYPE_CONFIRMABLE;

    if (!is_write(otCoapMessageGetCode(message))) {
        send_response(instance, message, info, OT_COAP_CODE_METHOD_NOT_ALLOWED, NULL);
        return;
    }

    cJSON *json = read_json_body(message);
    cJSON *zone = json ? cJSON_GetObjectItem(json, "zone") : NULL;
    cJSON *t    = json ? cJSON_GetObjectItem(json, "t")    : NULL;

    if (!cJSON_IsNumber(zone) || !cJSON_IsNumber(t)) {
        ESP_LOGW(TAG, "Temp denied (invalid JSON or zone-/ t-values)");
        cJSON_Delete(json);
        send_response(instance, message, info, OT_COAP_CODE_BAD_REQUEST, NULL);
        return;
    }

    bool ok = valve_control_update_temp((uint8_t)zone->valueint, (float)t->valuedouble);
    ESP_LOGI(TAG, "Temp zone=%d t=%.2f -> %s", zone->valueint, t->valuedouble,
             ok ? "ok" : "unbekannte Zone");
    cJSON_Delete(json);

    // Sensoren senden meist NON (Batterie) -> dann keine Antwort noetig
    if (is_con) {
        send_response(instance, message, info,
                      ok ? OT_COAP_CODE_CHANGED : OT_COAP_CODE_NOT_FOUND, NULL);
    }
}

// ---- PUT /heating/setpoint  {"zone":1,"sp":22.0} ---------------------------
static void setpoint_handler(void *ctx, otMessage *message, const otMessageInfo *info)
{
    otInstance *instance = esp_openthread_get_instance();

    if (!is_write(otCoapMessageGetCode(message))) {
        send_response(instance, message, info, OT_COAP_CODE_METHOD_NOT_ALLOWED, NULL);
        return;
    }

    cJSON *json = read_json_body(message);
    cJSON *zone = json ? cJSON_GetObjectItem(json, "zone") : NULL;
    cJSON *sp   = json ? cJSON_GetObjectItem(json, "sp")   : NULL;

    if (!cJSON_IsNumber(zone) || !cJSON_IsNumber(sp) ||
        sp->valuedouble < 5.0 || sp->valuedouble > 35.0) {
        ESP_LOGW(TAG, "Setpoint denied (invalid JSON or outside 5..35)");
        cJSON_Delete(json);
        send_response(instance, message, info, OT_COAP_CODE_BAD_REQUEST, NULL);
        return;
    }

    bool ok = valve_control_set_setpoint((uint8_t)zone->valueint, (float)sp->valuedouble);
    ESP_LOGI(TAG, "Setpoint zone=%d sp=%.1f -> %s", zone->valueint, sp->valuedouble,
        ok ? "ok" : "unknown zone");
    cJSON_Delete(json);

    if (!ok) {
        send_response(instance, message, info, OT_COAP_CODE_NOT_FOUND, NULL);
        return;
    }
    char *resp = build_status_json();
    send_response(instance, message, info, OT_COAP_CODE_CHANGED, resp);
    free(resp);
}

static void add_resource(otInstance *instance, otCoapResource *res,
                         const char *path, otCoapRequestHandler handler)
{
    res->mUriPath = path;
    res->mHandler = handler;
    res->mContext = NULL;
    res->mNext = NULL;
    otCoapAddResource(instance, res);
}

void coap_underfloor_heating_init(void)
{
    valve_control_init();

    otInstance *instance = esp_openthread_get_instance();

    esp_openthread_lock_acquire(portMAX_DELAY);
    otError err = otCoapStart(instance, OT_DEFAULT_COAP_PORT);
    if (err != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to start CoAP service: %d", err);
        esp_openthread_lock_release();
        return;
    }

    add_resource(instance, &s_res_status,   "heating",          status_handler);
    add_resource(instance, &s_res_temp,     "heating/temp",     temp_handler);
    add_resource(instance, &s_res_setpoint, "heating/setpoint", setpoint_handler);
    esp_openthread_lock_release();

    ESP_LOGI(TAG, "CoAP: /heating, /heating/temp, /heating/setpoint auf Port %d",
             OT_DEFAULT_COAP_PORT);
}
