#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ha_config.h"

#define HA_CONFIG_DEFAULT_PATH "config.example.json"

static ha_config_card_t g_cards[HA_CONFIG_MAX_CARDS];
static size_t g_card_count = 0;

static char g_tracked_entities[HA_CONFIG_MAX_TRACKED_ENTITIES][64];
static size_t g_tracked_entity_count = 0;

/* ---------- internal helpers ---------- */

static void ha_config_reset(void)
{
    memset(g_cards, 0, sizeof(g_cards));
    g_card_count = 0;
    memset(g_tracked_entities, 0, sizeof(g_tracked_entities));
    g_tracked_entity_count = 0;
}

static void ha_config_skip_ws(const char **p)
{
    const char *s = *p;
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    *p = s;
}

static int ha_config_expect_char(const char **p, char c)
{
    ha_config_skip_ws(p);
    if (**p != c) {
        fprintf(stderr, "[ha_config] JSON parse error: expected '%c', got '%c'\n",
                c, **p ? **p : '?');
        return 0;
    }
    (*p)++;
    return 1;
}

static int ha_config_parse_string(const char **p, char *out, size_t out_size)
{
    size_t len = 0;

    ha_config_skip_ws(p);
    if (**p != '\"') {
        fprintf(stderr, "[ha_config] JSON parse error: expected string\n");
        return 0;
    }
    (*p)++; /* skip opening quote */

    while (**p && **p != '\"') {
        unsigned char ch = (unsigned char)**p;
        if (ch == '\\') {
            (*p)++;
            if (!**p) {
                break;
            }
            ch = (unsigned char)**p;
            /* simple escape handling: store escaped char as-is */
        }
        if (len + 1 < out_size) {
            out[len++] = (char)ch;
        }
        (*p)++;
    }

    if (**p != '\"') {
        fprintf(stderr, "[ha_config] JSON parse error: unterminated string\n");
        return 0;
    }
    (*p)++; /* skip closing quote */

    if (len < out_size) {
        out[len] = '\0';
    } else if (out_size > 0) {
        out[out_size - 1] = '\0';
    }
    return 1;
}

static void ha_config_skip_json_string(const char **p)
{
    ha_config_skip_ws(p);
    if (**p != '\"') {
        return;
    }
    (*p)++; /* skip opening quote */
    while (**p) {
        if (**p == '\\') {
            (*p)++;
            if (**p) {
                (*p)++;
            }
        } else if (**p == '\"') {
            (*p)++;
            break;
        } else {
            (*p)++;
        }
    }
}

static void ha_config_skip_json_number(const char **p)
{
    ha_config_skip_ws(p);
    const char *s = *p;

    if (*s == '-' || *s == '+') {
        s++;
    }
    while (*s && (isdigit((unsigned char)*s) || *s == '.')) {
        s++;
    }
    if (*s == 'e' || *s == 'E') {
        s++;
        if (*s == '-' || *s == '+') {
            s++;
        }
        while (*s && isdigit((unsigned char)*s)) {
            s++;
        }
    }
    *p = s;
}

static void ha_config_skip_json_value(const char **p);

static void ha_config_skip_json_object(const char **p)
{
    if (!ha_config_expect_char(p, '{')) {
        return;
    }
    ha_config_skip_ws(p);
    while (**p && **p != '}') {
        ha_config_skip_json_string(p);
        ha_config_skip_ws(p);
        if (!ha_config_expect_char(p, ':')) {
            return;
        }
        ha_config_skip_ws(p);
        ha_config_skip_json_value(p);
        ha_config_skip_ws(p);
        if (**p == ',') {
            (*p)++;
            ha_config_skip_ws(p);
        } else {
            break;
        }
    }
    if (**p == '}') {
        (*p)++;
    }
}

static void ha_config_skip_json_array(const char **p)
{
    if (!ha_config_expect_char(p, '[')) {
        return;
    }
    ha_config_skip_ws(p);
    while (**p && **p != ']') {
        ha_config_skip_json_value(p);
        ha_config_skip_ws(p);
        if (**p == ',') {
            (*p)++;
            ha_config_skip_ws(p);
        } else {
            break;
        }
    }
    if (**p == ']') {
        (*p)++;
    }
}

static void ha_config_skip_json_literal(const char **p, const char *lit)
{
    size_t n = strlen(lit);
    if (strncmp(*p, lit, n) == 0) {
        *p += n;
    }
}

static void ha_config_skip_json_value(const char **p)
{
    ha_config_skip_ws(p);
    char c = **p;

    if (c == '\"') {
        ha_config_skip_json_string(p);
    } else if (c == '{') {
        ha_config_skip_json_object(p);
    } else if (c == '[') {
        ha_config_skip_json_array(p);
    } else if (c == '-' || c == '+' || isdigit((unsigned char)c)) {
        ha_config_skip_json_number(p);
    } else if (c == 't') {
        ha_config_skip_json_literal(p, "true");
    } else if (c == 'f') {
        ha_config_skip_json_literal(p, "false");
    } else if (c == 'n') {
        ha_config_skip_json_literal(p, "null");
    } else if (c != '\0') {
        (*p)++;
    }
}

static void ha_config_add_tracked_entity(const char *entity_id)
{
    size_t i;

    if (!entity_id || !*entity_id) {
        return;
    }

    for (i = 0; i < g_tracked_entity_count; i++) {
        if (strncmp(g_tracked_entities[i], entity_id,
                    sizeof(g_tracked_entities[i])) == 0) {
            return; /* already present */
        }
    }

    if (g_tracked_entity_count >= HA_CONFIG_MAX_TRACKED_ENTITIES) {
        fprintf(stderr,
                "[ha_config] Tracked-entity limit reached (%d)\n",
                HA_CONFIG_MAX_TRACKED_ENTITIES);
        return;
    }

    strncpy(g_tracked_entities[g_tracked_entity_count],
            entity_id,
            sizeof(g_tracked_entities[g_tracked_entity_count]) - 1);
    g_tracked_entities[g_tracked_entity_count]
        [sizeof(g_tracked_entities[g_tracked_entity_count]) - 1] = '\0';
    g_tracked_entity_count++;
}

static int ha_config_parse_action_data_object(const char **p,
                                              ha_config_action_t *action)
{
    if (!ha_config_expect_char(p, '{')) {
        return 0;
    }
    ha_config_skip_ws(p);

    while (**p && **p != '}') {
        char key[32];

        if (!ha_config_parse_string(p, key, sizeof(key))) {
            return 0;
        }
        ha_config_skip_ws(p);
        if (!ha_config_expect_char(p, ':')) {
            return 0;
        }
        ha_config_skip_ws(p);

        if (action->data_count >= HA_CONFIG_MAX_ACTION_DATA) {
            ha_config_skip_json_value(p);
        } else {
            char value[64] = {0};

            if (**p == '\"') {
                if (!ha_config_parse_string(p, value, sizeof(value))) {
                    return 0;
                }
            } else {
                const char *before = *p;
                ha_config_skip_json_number(p);
                size_t len = (size_t)(*p - before);
                if (len >= sizeof(value)) {
                    len = sizeof(value) - 1;
                }
                memcpy(value, before, len);
                value[len] = '\0';
            }

            strncpy(action->data[action->data_count].key,
                    key,
                    sizeof(action->data[action->data_count].key) - 1);
            action->data[action->data_count]
                .key[sizeof(action->data[action->data_count].key) - 1] = '\0';

            strncpy(action->data[action->data_count].value,
                    value,
                    sizeof(action->data[action->data_count].value) - 1);
            action->data[action->data_count]
                .value[sizeof(action->data[action->data_count].value) - 1] = '\0';

            action->data_count++;
        }

        ha_config_skip_ws(p);
        if (**p == ',') {
            (*p)++;
            ha_config_skip_ws(p);
        } else {
            break;
        }
    }

    if (**p == '}') {
        (*p)++;
    } else {
        fprintf(stderr,
                "[ha_config] JSON parse error: unterminated data object\n");
        return 0;
    }

    return 1;
}

static int ha_config_parse_action_object(const char **p,
                                         ha_config_action_t *action)
{
    if (!ha_config_expect_char(p, '{')) {
        return 0;
    }
    ha_config_skip_ws(p);

    while (**p && **p != '}') {
        char key[32];

        if (!ha_config_parse_string(p, key, sizeof(key))) {
            return 0;
        }
        ha_config_skip_ws(p);
        if (!ha_config_expect_char(p, ':')) {
            return 0;
        }
        ha_config_skip_ws(p);

        if (strcmp(key, "service") == 0) {
            if (!ha_config_parse_string(p,
                                        action->service,
                                        sizeof(action->service))) {
                return 0;
            }
        } else if (strcmp(key, "data") == 0) {
            if (!ha_config_parse_action_data_object(p, action)) {
                return 0;
            }
        } else {
            ha_config_skip_json_value(p);
        }

        ha_config_skip_ws(p);
        if (**p == ',') {
            (*p)++;
            ha_config_skip_ws(p);
        } else {
            break;
        }
    }

    if (**p == '}') {
        (*p)++;
    } else {
        fprintf(stderr,
                "[ha_config] JSON parse error: unterminated action object\n");
        return 0;
    }

    return 1;
}

static int ha_config_parse_options_object(const char **p,
                                          ha_config_options_t *opts)
{
    if (!ha_config_expect_char(p, '{')) {
        return 0;
    }
    ha_config_skip_ws(p);

    while (**p && **p != '}') {
        char key[32];
        double num = 0.0;
        char *endptr = NULL;
        const char *start;

        if (!ha_config_parse_string(p, key, sizeof(key))) {
            return 0;
        }
        ha_config_skip_ws(p);
        if (!ha_config_expect_char(p, ':')) {
            return 0;
        }
        ha_config_skip_ws(p);

        start = *p;
        if (**p == '\"') {
            char tmp[32];
            if (!ha_config_parse_string(p, tmp, sizeof(tmp))) {
                return 0;
            }
            num = strtod(tmp, &endptr);
        } else {
            num = strtod(start, &endptr);
            if (endptr == start) {
                ha_config_skip_json_value(p);
            } else {
                *p = endptr;
            }
        }

        if (strcmp(key, "brightness_step") == 0) {
            opts->brightness_step = num;
            opts->has_brightness_step = 1;
        } else if (strcmp(key, "color_temp_step") == 0) {
            opts->color_temp_step = num;
            opts->has_color_temp_step = 1;
        } else if (strcmp(key, "temp_step") == 0) {
            opts->temp_step = num;
            opts->has_temp_step = 1;
        } else {
            /* unknown option; value already consumed */
        }

        ha_config_skip_ws(p);
        if (**p == ',') {
            (*p)++;
            ha_config_skip_ws(p);
        } else {
            break;
        }
    }

    if (**p == '}') {
        (*p)++;
    } else {
        fprintf(stderr,
                "[ha_config] JSON parse error: unterminated options object\n");
        return 0;
    }

    return 1;
}

static int ha_config_parse_card_object(const char **p, ha_config_card_t *card)
{
    memset(card, 0, sizeof(*card));

    if (!ha_config_expect_char(p, '{')) {
        return 0;
    }
    ha_config_skip_ws(p);

    while (**p && **p != '}') {
        char key[32];

        if (!ha_config_parse_string(p, key, sizeof(key))) {
            return 0;
        }
        ha_config_skip_ws(p);
        if (!ha_config_expect_char(p, ':')) {
            return 0;
        }
        ha_config_skip_ws(p);

        if (strcmp(key, "type") == 0) {
            if (!ha_config_parse_string(p,
                                        card->type,
                                        sizeof(card->type))) {
                return 0;
            }
        } else if (strcmp(key, "entity_id") == 0) {
            if (!ha_config_parse_string(p,
                                        card->entity_id,
                                        sizeof(card->entity_id))) {
                return 0;
            }
        } else if (strcmp(key, "title") == 0) {
            if (!ha_config_parse_string(p,
                                        card->title,
                                        sizeof(card->title))) {
                return 0;
            }
        } else if (strcmp(key, "options") == 0) {
            if (!ha_config_parse_options_object(p, &card->options)) {
                return 0;
            }
        } else if (strcmp(key, "actions") == 0) {
            if (!ha_config_expect_char(p, '{')) {
                return 0;
            }
            ha_config_skip_ws(p);

            while (**p && **p != '}') {
                char subkey[32];

                if (!ha_config_parse_string(p, subkey, sizeof(subkey))) {
                    return 0;
                }
                ha_config_skip_ws(p);
                if (!ha_config_expect_char(p, ':')) {
                    return 0;
                }
                ha_config_skip_ws(p);

                if (strcmp(subkey, "primary") == 0) {
                    if (!ha_config_parse_action_object(p, &card->primary)) {
                        return 0;
                    }
                } else if (strcmp(subkey, "secondary") == 0) {
                    if (!ha_config_parse_action_object(p, &card->secondary)) {
                        return 0;
                    }
                } else {
                    ha_config_skip_json_value(p);
                }

                ha_config_skip_ws(p);
                if (**p == ',') {
                    (*p)++;
                    ha_config_skip_ws(p);
                } else {
                    break;
                }
            }

            if (**p == '}') {
                (*p)++;
            } else {
                fprintf(stderr,
                        "[ha_config] JSON parse error: "
                        "unterminated actions object wrapper\n");
                return 0;
            }
        } else {
            ha_config_skip_json_value(p);
        }

        ha_config_skip_ws(p);
        if (**p == ',') {
            (*p)++;
            ha_config_skip_ws(p);
        } else {
            break;
        }
    }

    if (**p == '}') {
        (*p)++;
    } else {
        fprintf(stderr,
                "[ha_config] JSON parse error: unterminated card object\n");
        return 0;
    }

    if (card->type[0] == '\0' ||
        card->entity_id[0] == '\0' ||
        card->title[0] == '\0') {
        fprintf(stderr,
                "[ha_config] Skipping card with missing required fields\n");
        return 0;
    }

    ha_config_add_tracked_entity(card->entity_id);
    return 1;
}

static int ha_config_parse_cards_array(const char **p)
{
    if (!ha_config_expect_char(p, '[')) {
        return 0;
    }
    ha_config_skip_ws(p);

    ha_config_reset();

    while (**p && **p != ']') {
        if (g_card_count >= HA_CONFIG_MAX_CARDS) {
            fprintf(stderr,
                    "[ha_config] Card limit reached (%d); skipping remaining\n",
                    HA_CONFIG_MAX_CARDS);
            ha_config_skip_json_value(p);
        } else {
            if (!ha_config_parse_card_object(p, &g_cards[g_card_count])) {
                ha_config_skip_json_value(p);
            } else {
                g_card_count++;
            }
        }

        ha_config_skip_ws(p);
        if (**p == ',') {
            (*p)++;
            ha_config_skip_ws(p);
        } else {
            break;
        }
    }

    if (**p == ']') {
        (*p)++;
    } else {
        fprintf(stderr,
                "[ha_config] JSON parse error: unterminated cards array\n");
        return 0;
    }

    return 1;
}

static int ha_config_parse_root(const char *buf)
{
    const char *p = buf;
    int found_cards = 0;

    ha_config_skip_ws(&p);
    if (!ha_config_expect_char(&p, '{')) {
        return 0;
    }
    ha_config_skip_ws(&p);

    while (*p && *p != '}') {
        char key[32];

        if (!ha_config_parse_string(&p, key, sizeof(key))) {
            return 0;
        }
        ha_config_skip_ws(&p);
        if (!ha_config_expect_char(&p, ':')) {
            return 0;
        }
        ha_config_skip_ws(&p);

        if (strcmp(key, "cards") == 0) {
            if (!ha_config_parse_cards_array(&p)) {
                return 0;
            }
            found_cards = 1;
        } else {
            ha_config_skip_json_value(&p);
        }

        ha_config_skip_ws(&p);
        if (*p == ',') {
            p++;
            ha_config_skip_ws(&p);
        } else {
            break;
        }
    }

    if (*p == '}') {
        p++;
    } else {
        fprintf(stderr,
                "[ha_config] JSON parse error: unterminated root object\n");
        return 0;
    }

    if (!found_cards) {
        fprintf(stderr, "[ha_config] No 'cards' array in config\n");
        return 0;
    }

    return 1;
}

static void ha_config_log_loaded(void)
{
    size_t i, j;

    fprintf(stderr, "[ha_config] Loaded %lu card(s)\n",
            (unsigned long)g_card_count);

    for (i = 0; i < g_card_count; i++) {
        const ha_config_card_t *c = &g_cards[i];

        fprintf(stderr,
                "[ha_config] Card %lu: type=%s, entity_id=%s, title=\"%s\"\n",
                (unsigned long)i,
                c->type,
                c->entity_id,
                c->title);

        if (c->primary.service[0]) {
            fprintf(stderr,
                    "[ha_config]   primary.service=%s\n",
                    c->primary.service);
            for (j = 0; j < (size_t)c->primary.data_count; j++) {
                fprintf(stderr,
                        "[ha_config]     primary.data[%lu]: %s=%s\n",
                        (unsigned long)j,
                        c->primary.data[j].key,
                        c->primary.data[j].value);
            }
        } else {
            fprintf(stderr,
                    "[ha_config]   primary.service=<none>\n");
        }

        if (c->secondary.service[0]) {
            fprintf(stderr,
                    "[ha_config]   secondary.service=%s\n",
                    c->secondary.service);
            for (j = 0; j < (size_t)c->secondary.data_count; j++) {
                fprintf(stderr,
                        "[ha_config]     secondary.data[%lu]: %s=%s\n",
                        (unsigned long)j,
                        c->secondary.data[j].key,
                        c->secondary.data[j].value);
            }
        } else {
            fprintf(stderr,
                    "[ha_config]   secondary.service=<none>\n");
        }
    }

    fprintf(stderr,
            "[ha_config] Tracked entities (%lu):\n",
            (unsigned long)g_tracked_entity_count);
    for (i = 0; i < g_tracked_entity_count; i++) {
        fprintf(stderr,
                "[ha_config]   %s\n",
                g_tracked_entities[i]);
    }
}

/* ---------- public API ---------- */

int ha_config_load(const char *path)
{
    const char *use_path = path ? path : HA_CONFIG_DEFAULT_PATH;
    FILE *fp;
    long len;
    size_t read_len;
    char *buf;
    int ok;

    ha_config_reset();

    fp = fopen(use_path, "rb");
    if (!fp) {
        fprintf(stderr,
                "[ha_config] Could not open config file '%s': %s\n",
                use_path, strerror(errno));
        return 0;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fprintf(stderr,
                "[ha_config] fseek failed for '%s'\n", use_path);
        fclose(fp);
        return 0;
    }
    len = ftell(fp);
    if (len < 0) {
        fprintf(stderr,
                "[ha_config] ftell failed for '%s'\n", use_path);
        fclose(fp);
        return 0;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fprintf(stderr,
                "[ha_config] fseek(SEEK_SET) failed for '%s'\n", use_path);
        fclose(fp);
        return 0;
    }

    buf = (char *)malloc((size_t)len + 1);
    if (!buf) {
        fprintf(stderr,
                "[ha_config] Out of memory reading '%s' (len=%ld)\n",
                use_path, len);
        fclose(fp);
        return 0;
    }

    read_len = fread(buf, 1, (size_t)len, fp);
    fclose(fp);
    if (read_len != (size_t)len) {
        fprintf(stderr,
                "[ha_config] fread mismatch for '%s' (expected=%ld, got=%zu)\n",
                use_path, len, read_len);
        free(buf);
        return 0;
    }
    buf[len] = '\0';

    ok = ha_config_parse_root(buf);
    free(buf);

    if (!ok) {
        fprintf(stderr,
                "[ha_config] Failed to parse config file '%s'\n", use_path);
        ha_config_reset();
        return 0;
    }

    ha_config_log_loaded();
    return 1;
}

size_t ha_config_get_card_count(void)
{
    return g_card_count;
}

const ha_config_card_t *ha_config_get_card(size_t index)
{
    if (index >= g_card_count) {
        return NULL;
    }
    return &g_cards[index];
}

size_t ha_config_get_tracked_entity_count(void)
{
    return g_tracked_entity_count;
}

const char *ha_config_get_tracked_entity(size_t index)
{
    if (index >= g_tracked_entity_count) {
        return NULL;
    }
    return g_tracked_entities[index];
}
