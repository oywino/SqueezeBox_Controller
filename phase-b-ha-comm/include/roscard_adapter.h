#ifndef ROSCARD_ADAPTER_H
#define ROSCARD_ADAPTER_H

/* RosCard Adapter — Public Interface (Step 8.2)
 *
 * Scope:
 *   - Header-only boundary for integrating RosCard JSON with the device.
 *   - No implementation or side effects are introduced at this step.
 *   - Stable, minimal exported surface for auditability and loose coupling.
 *
 * Non-goals (Step 8.2):
 *   - No parsing, rendering, or HA calls.
 *   - No dependence on LVGL or fb.c lifecycle.
 *   - No schema mutation; upstream RosCard semantics are preserved.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Semantic version of the adapter interface (header). */
#define ROSCARD_ADAPTER_API_VERSION "0.1.0-step8.2"

/* Error codes for adapter operations. */
typedef enum {
    ROSCARD_OK = 0,
    ROSCARD_EINVAL,        /* Invalid arguments or JSON */
    ROSCARD_ENOMEM,        /* Allocation failure */
    ROSCARD_ESTATE,        /* Invalid state for requested operation */
    ROSCARD_EUNSUPPORTED,  /* Unsupported RosCard feature for device scope */
    ROSCARD_EINTERNAL      /* Internal adapter error (reserved) */
} roscard_err_t;

/* Opaque handles to keep the boundary minimal and stable.
 * The actual structures are private to the adapter implementation.
 */
typedef struct roscard_adapter roscard_adapter_t;
typedef struct roscard_doc roscard_doc_t;

/* Lifecycle: adapter context.
 * Context holds no external resources in Step 8.2; created/destroyed deterministically.
 */
roscard_adapter_t* roscard_adapter_create(void);
void roscard_adapter_destroy(roscard_adapter_t* ctx);

/* Parse a RosCard JSON document into an internal, immutable representation.
 * - 'json' must be a UTF-8, NUL-terminated string.
 * - On success, '*out_doc' is set and owned by the caller; free with roscard_doc_free().
 * - The parsed representation preserves upstream semantics; unknown fields are retained for later mapping.
 */
roscard_err_t roscard_parse(const char* json, roscard_doc_t** out_doc);

/* Free a parsed RosCard document. Safe to call with NULL (no-op). */
void roscard_doc_free(roscard_doc_t* doc);

/* Introspection helpers (implementation-free contract).
 * Note: All returned views/pointers are valid until roscard_doc_free().
 */

/* Total number of top-level cards contained in the document. */
size_t roscard_doc_card_count(const roscard_doc_t* doc);

/* Return a readonly JSON slice for the Nth card (0-based).
 * - 'out_json' receives a pointer to a memory range valid while 'doc' is alive.
 * - If 'out_len' is non-NULL, it receives the byte length (not including any terminating NUL).
 */
roscard_err_t roscard_doc_get_card_json(const roscard_doc_t* doc,
                                        size_t index,
                                        const char** out_json,
                                        size_t* out_len);

/* Optional: Retrieve a readonly JSON slice for the entire document as normalized text. */
roscard_err_t roscard_doc_get_json(const roscard_doc_t* doc,
                                   const char** out_json,
                                   size_t* out_len);

/* Placeholder for future mapping boundary (no implementation in Step 8.2).
 * This function will, in later steps, translate a single RosCard card JSON
 * into the device's internal card descriptor/structs.
 * Do not implement or link this in Step 8.2.
 */
typedef struct roscard_device_card roscard_device_card_t; /* forward-declared; defined on device side */
roscard_err_t roscard_translate_card(const char* card_json,
                                     roscard_device_card_t* out_card,
                                     size_t out_card_size);

/* Header-only guard to ensure the boundary compiles in isolation. */
static inline int roscard_header_sanity(void) { return 1; }

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROSCARD_ADAPTER_H */
