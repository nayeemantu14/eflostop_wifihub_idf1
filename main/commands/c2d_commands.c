#include "c2d_commands.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "cJSON.h"

#define C2D_TAG "C2D_CMD"

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

static bool parse_envelope(const char *json_str, c2d_command_t *cmd_out);
static bool parse_legacy(const char *text, c2d_command_t *cmd_out);

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool c2d_command_parse(const char *data, size_t data_len, c2d_command_t *cmd_out)
{
    if (!data || data_len == 0 || !cmd_out) return false;

    memset(cmd_out, 0, sizeof(*cmd_out));

    // Create null-terminated copy
    char *buf = (char *)malloc(data_len + 1);
    if (!buf) return false;
    memcpy(buf, data, data_len);
    buf[data_len] = '\0';

    // Trim leading whitespace
    char *trimmed = buf;
    while (*trimmed == ' ' || *trimmed == '\t' || *trimmed == '\n' || *trimmed == '\r')
        trimmed++;

    bool ok = false;

    // Try JSON envelope first (starts with '{' and has our schema)
    if (trimmed[0] == '{') {
        ok = parse_envelope(trimmed, cmd_out);
    }

    // Fall back to legacy text parsing
    if (!ok) {
        ok = parse_legacy(trimmed, cmd_out);
    }

    free(buf);
    return ok;
}

void c2d_command_free(c2d_command_t *cmd)
{
    if (cmd && cmd->payload_json) {
        free(cmd->payload_json);
        cmd->payload_json = NULL;
    }
}

// ---------------------------------------------------------------------------
// JSON envelope parser (accepts canonical and legacy schema strings)
// ---------------------------------------------------------------------------

static bool parse_envelope(const char *json_str, c2d_command_t *cmd_out)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return false;

    // Check schema field — accept both canonical and legacy
    cJSON *schema = cJSON_GetObjectItem(root, "schema");
    if (!cJSON_IsString(schema)) {
        cJSON_Delete(root);
        return false;
    }

    bool is_canonical = (strcmp(schema->valuestring, C2D_CMD_SCHEMA) == 0);
    bool is_legacy    = (strcmp(schema->valuestring, C2D_CMD_SCHEMA_LEGACY) == 0);

    if (!is_canonical && !is_legacy) {
        cJSON_Delete(root);
        return false;
    }

    // Extract version
    if (is_canonical) {
        cJSON *ver_field = cJSON_GetObjectItem(root, "ver");
        cmd_out->ver = cJSON_IsNumber(ver_field) ? (int)ver_field->valuedouble : 1;
    } else {
        // Legacy "eflostop.cmd.v1" implies ver=1
        cmd_out->ver = 1;
    }

    // Extract cmd (required)
    cJSON *cmd_field = cJSON_GetObjectItem(root, "cmd");
    if (!cJSON_IsString(cmd_field) || strlen(cmd_field->valuestring) == 0) {
        ESP_LOGW(C2D_TAG, "Envelope missing 'cmd' field");
        cJSON_Delete(root);
        return false;
    }
    strncpy(cmd_out->cmd, cmd_field->valuestring, sizeof(cmd_out->cmd) - 1);

    // Extract optional correlation id
    cJSON *id_field = cJSON_GetObjectItem(root, "id");
    if (cJSON_IsString(id_field)) {
        strncpy(cmd_out->id, id_field->valuestring, sizeof(cmd_out->id) - 1);
    }

    // Extract optional payload (as stringified JSON)
    cJSON *payload = cJSON_GetObjectItem(root, "payload");
    if (payload && !cJSON_IsNull(payload)) {
        char *pstr = cJSON_PrintUnformatted(payload);
        if (pstr) {
            cmd_out->payload_json = pstr;  // caller frees via c2d_command_free
        }
    }

    cmd_out->is_envelope = true;
    cJSON_Delete(root);

    ESP_LOGI(C2D_TAG, "Envelope cmd='%s' ver=%d id='%s' payload=%s",
             cmd_out->cmd, cmd_out->ver, cmd_out->id,
             cmd_out->payload_json ? cmd_out->payload_json : "(none)");
    return true;
}

// ---------------------------------------------------------------------------
// Legacy text parser
// ---------------------------------------------------------------------------

/** Helper: duplicate a string onto the heap (for payload_json). */
static char *dup_str(const char *s)
{
    if (!s || !*s) return NULL;
    size_t len = strlen(s);
    char *d = (char *)malloc(len + 1);
    if (d) { memcpy(d, s, len); d[len] = '\0'; }
    return d;
}

static bool parse_legacy(const char *text, c2d_command_t *cmd_out)
{
    cmd_out->is_envelope = false;
    cmd_out->ver = 0;

    // ---- Decommission family (check specific before general) ----

    if (strstr(text, "DECOMMISSION_VALVE")) {
        strncpy(cmd_out->cmd, C2D_CMD_DECOMMISSION, sizeof(cmd_out->cmd) - 1);
        cmd_out->payload_json = dup_str("{\"target\":\"valve\"}");
        return true;
    }

    if (strstr(text, "DECOMMISSION_LORA:")) {
        const char *id_str = strstr(text, "DECOMMISSION_LORA:") +
                             strlen("DECOMMISSION_LORA:");
        // Parse hex sensor ID
        uint32_t sid = (uint32_t)strtoul(id_str, NULL, 16);
        char payload[80];
        snprintf(payload, sizeof(payload),
                 "{\"target\":\"lora\",\"sensor_id\":\"0x%08lX\"}",
                 (unsigned long)sid);
        strncpy(cmd_out->cmd, C2D_CMD_DECOMMISSION, sizeof(cmd_out->cmd) - 1);
        cmd_out->payload_json = dup_str(payload);
        return true;
    }

    if (strstr(text, "DECOMMISSION_BLE:")) {
        const char *mac_start = strstr(text, "DECOMMISSION_BLE:") +
                                strlen("DECOMMISSION_BLE:");
        // Extract MAC (up to 17 chars, stop at whitespace)
        char mac_clean[18] = {0};
        int i = 0;
        while (*mac_start && i < 17 &&
               *mac_start != ' ' && *mac_start != '\t' &&
               *mac_start != '\n' && *mac_start != '\r') {
            mac_clean[i++] = *mac_start++;
        }
        char payload[80];
        snprintf(payload, sizeof(payload),
                 "{\"target\":\"ble\",\"sensor_id\":\"%s\"}", mac_clean);
        strncpy(cmd_out->cmd, C2D_CMD_DECOMMISSION, sizeof(cmd_out->cmd) - 1);
        cmd_out->payload_json = dup_str(payload);
        return true;
    }

    if (strstr(text, "DECOMMISSION_ALL") ||
        strcmp(text, "DECOMMISSION") == 0) {
        strncpy(cmd_out->cmd, C2D_CMD_DECOMMISSION, sizeof(cmd_out->cmd) - 1);
        cmd_out->payload_json = dup_str("{\"target\":\"all\"}");
        return true;
    }

    // Invalid DECOMMISSION_LORA / DECOMMISSION_BLE without colon
    if (strstr(text, "DECOMMISSION_LORA") ||
        strstr(text, "DECOMMISSION_BLE")) {
        ESP_LOGE(C2D_TAG, "Invalid decommission format (missing ':')");
        return false;
    }

    // ---- Valve control ----

    if (strstr(text, "VALVE_OPEN")) {
        strncpy(cmd_out->cmd, C2D_CMD_VALVE_OPEN, sizeof(cmd_out->cmd) - 1);
        return true;
    }

    if (strstr(text, "VALVE_CLOSE")) {
        strncpy(cmd_out->cmd, C2D_CMD_VALVE_CLOSE, sizeof(cmd_out->cmd) - 1);
        return true;
    }

    // ---- Configuration commands (with JSON payload after ':') ----

    if (strstr(text, "RULES_CONFIG:")) {
        const char *json_str = strstr(text, "RULES_CONFIG:") +
                               strlen("RULES_CONFIG:");
        strncpy(cmd_out->cmd, C2D_CMD_RULES_CONFIG, sizeof(cmd_out->cmd) - 1);
        cmd_out->payload_json = dup_str(json_str);
        return true;
    }

    if (strstr(text, "SENSOR_META:")) {
        const char *json_str = strstr(text, "SENSOR_META:") +
                               strlen("SENSOR_META:");
        strncpy(cmd_out->cmd, C2D_CMD_SENSOR_META, sizeof(cmd_out->cmd) - 1);
        cmd_out->payload_json = dup_str(json_str);
        return true;
    }

    // ---- Leak reset ----

    if (strstr(text, "LEAK_RESET")) {
        strncpy(cmd_out->cmd, C2D_CMD_LEAK_RESET, sizeof(cmd_out->cmd) - 1);
        return true;
    }

    // ---- JSON provisioning payload (fallback) ----

    if (text[0] == '{') {
        strncpy(cmd_out->cmd, C2D_CMD_PROVISION, sizeof(cmd_out->cmd) - 1);
        cmd_out->payload_json = dup_str(text);
        return true;
    }

    ESP_LOGW(C2D_TAG, "Unrecognized C2D command: %.40s%s",
             text, strlen(text) > 40 ? "..." : "");
    return false;
}
