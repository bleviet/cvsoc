/*
 * ua_micro.h — Lightweight ANSI C99 OPC UA Binary (opc.tcp) & Part 14 UADP
 *              PubSub runtime for embedded FPGA+SoC targets.
 *
 * Implements the subset of IEC 62541-6 (OPC UA Binary Wire Protocol) and
 * IEC 62541-14 (UADP UDP PubSub) needed to expose hardware MMIO registers as
 * a browsable, self-describing OPC UA Address Space with:
 *   - TCP Connection Protocol: HEL / ACK / OPN / MSG / CLO framing
 *   - Built-in Services: Browse (527), Read (631), Write (673), Call (712),
 *     CreateSubscription (787), and Publish / DataChangeNotification (826)
 *   - Hardware-backed UA_DataSource read/write callbacks
 *   - UADP Binary PubSub NetworkMessage encoder over UDP
 *
 * Modeled after the open62541 C API so applications can understand the exact
 * concepts (UA_NodeId, UA_Variant, UA_DataSource, UA_StatusCode) with zero
 * external build dependencies.
 */

#ifndef UA_MICRO_H
#define UA_MICRO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Standard OPC UA Ports (IEC 62541) ───────────────────────────────────── */
#define UA_DEFAULT_TCP_PORT     4840
#define UA_DEFAULT_PUBSUB_PORT  4841

/* ── Standard OPC UA StatusCodes (IEC 62541-6) ───────────────────────────── */
typedef uint32_t UA_StatusCode;

#define UA_STATUSCODE_GOOD                  0x00000000u
#define UA_STATUSCODE_BADINTERNALERROR      0x80020000u
#define UA_STATUSCODE_BADDECODINGERROR      0x80070000u
#define UA_STATUSCODE_BADSERVICEUNSUPPORTED 0x800B0000u
#define UA_STATUSCODE_BADNODEIDUNKNOWN      0x80340000u
#define UA_STATUSCODE_BADNOTREADABLE        0x803A0000u
#define UA_STATUSCODE_BADNOTWRITABLE        0x803B0000u
#define UA_STATUSCODE_BADOUTOFRANGE         0x803C0000u
#define UA_STATUSCODE_BADTYPEMISMATCH       0x80740000u
#define UA_STATUSCODE_BADMETHODINVALID      0x80750000u
#define UA_STATUSCODE_BADINVALIDARGUMENT    0x80AB0000u

/* ── Standard OPC UA Built-in Type IDs (IEC 62541-6 Table 1) ─────────────── */
typedef enum {
    UA_TYPE_BOOLEAN = 1,   /* 1-byte bool (0 or 1) */
    UA_TYPE_BYTE    = 3,   /* 1-byte unsigned (uint8_t) */
    UA_TYPE_UINT32  = 7,   /* 4-byte unsigned little-endian (uint32_t) */
    UA_TYPE_STRING  = 12   /* Length-prefixed UTF-8 string */
} UA_BuiltinType;

/* ── Standard OPC UA NodeClasses (IEC 62541-3) ───────────────────────────── */
typedef enum {
    UA_NODECLASS_OBJECT   = 1,
    UA_NODECLASS_VARIABLE = 2,
    UA_NODECLASS_METHOD   = 4
} UA_NodeClass;

/* ── Standard OPC UA AccessLevel Masks (IEC 62541-3) ─────────────────────── */
#define UA_ACCESSLEVELMASK_READ       0x01u
#define UA_ACCESSLEVELMASK_WRITE      0x02u
#define UA_ACCESSLEVELMASK_READWRITE  (UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE)

/* ── Standard OPC UA Binary Service NodeIds (Namespace 0, Four-Byte 0x01) ── */
#define UA_SVC_BROWSE_REQ             527u
#define UA_SVC_BROWSE_RESP            530u
#define UA_SVC_READ_REQ               631u
#define UA_SVC_READ_RESP              634u
#define UA_SVC_WRITE_REQ              673u
#define UA_SVC_WRITE_RESP             676u
#define UA_SVC_CALL_REQ               712u
#define UA_SVC_CALL_RESP              715u
#define UA_SVC_CREATESUB_REQ          787u
#define UA_SVC_CREATESUB_RESP         790u
#define UA_SVC_PUBLISH_REQ            826u
#define UA_SVC_PUBLISH_RESP           829u

/* ── Limits for Embedded Static Allocation (Zero malloc in hot loop) ─────── */
#define UA_MAX_STRING_LEN   64
#define UA_MAX_NODES        32
#define UA_MAX_CLIENTS      8
#define UA_MAX_PACKET_SIZE  4096

/* ── UA_NodeId: String NodeId in Namespace (e.g., ns=1;s="fpga.led_pattern") */
typedef struct {
    uint16_t namespace_index;
    char     identifier[UA_MAX_STRING_LEN];
} UA_NodeId;

/* ── UA_Variant: Dynamically-typed value container ───────────────────────── */
typedef struct {
    UA_BuiltinType type;
    union {
        bool     b;
        uint8_t  u8;
        uint32_t u32;
        char     str[UA_MAX_STRING_LEN];
    } value;
} UA_Variant;

/* ── Hardware DataSource Callbacks (called on Read / Write) ──────────────── */
typedef UA_StatusCode (*UA_ReadCallback)(void *user_context,
                                         const UA_NodeId *node_id,
                                         UA_Variant *out_value);

typedef UA_StatusCode (*UA_WriteCallback)(void *user_context,
                                          const UA_NodeId *node_id,
                                          const UA_Variant *in_value);

typedef UA_StatusCode (*UA_MethodCallback)(void *user_context,
                                           const UA_NodeId *method_id,
                                           const UA_Variant *input_arg,
                                           UA_Variant *output_arg);

typedef struct {
    UA_ReadCallback  read;
    UA_WriteCallback write;
} UA_DataSource;

/* ── Address Space Node Definition ───────────────────────────────────────── */
typedef struct {
    UA_NodeId         node_id;
    char              parent_id[UA_MAX_STRING_LEN];
    char              browse_name[UA_MAX_STRING_LEN];
    char              display_name[UA_MAX_STRING_LEN];
    char              description[UA_MAX_STRING_LEN];
    UA_NodeClass      node_class;
    UA_BuiltinType    data_type;
    uint8_t           access_level;
    UA_DataSource     data_source;
    UA_MethodCallback method_cb;
    void             *user_context;
} UA_Node;

/* ── Server Context ──────────────────────────────────────────────────────── */
typedef struct {
    UA_Node  nodes[UA_MAX_NODES];
    size_t   node_count;
    char     endpoint_url[UA_MAX_STRING_LEN];
} UA_Server;

/* ── Server Lifecycle & Address Space Registration API ───────────────────── */
void UA_Server_init(UA_Server *server, const char *endpoint_url);

UA_NodeId UA_NODEID_STRING(uint16_t ns_idx, const char *id_str);

UA_StatusCode UA_Server_addObjectNode(UA_Server *server,
                                      UA_NodeId node_id,
                                      const char *parent_id,
                                      const char *browse_name,
                                      const char *display_name,
                                      const char *description);

UA_StatusCode UA_Server_addDataSourceVariableNode(UA_Server *server,
                                                  UA_NodeId node_id,
                                                  const char *parent_id,
                                                  const char *browse_name,
                                                  const char *display_name,
                                                  const char *description,
                                                  UA_BuiltinType data_type,
                                                  uint8_t access_level,
                                                  UA_DataSource data_source,
                                                  void *user_context);

UA_StatusCode UA_Server_addMethodNode(UA_Server *server,
                                      UA_NodeId node_id,
                                      const char *parent_id,
                                      const char *browse_name,
                                      const char *display_name,
                                      const char *description,
                                      UA_MethodCallback method_cb,
                                      void *user_context);

const UA_Node *UA_Server_findNode(const UA_Server *server,
                                  uint16_t ns_idx,
                                  const char *identifier);

/*
 * Process a single incoming OPC UA TCP chunk (`HEL`, `OPN`, `MSG`, `CLO`)
 * and write the response chunk into `tx_buf`.
 * Returns number of bytes to send (0 if connection should close, <0 on error).
 */
int UA_Server_processChunk(UA_Server *server,
                           const uint8_t *rx_buf,
                           size_t rx_len,
                           uint8_t *tx_buf,
                           size_t tx_cap,
                           bool *out_should_close);

/*
 * Encode a DataChangeNotification `MSG` chunk (`UA_SVC_PUBLISH_RESP` = 829)
 * when a monitored node changes value.
 */
size_t UA_Server_encodeDataChangeNotification(uint32_t secure_channel_id,
                                              uint32_t token_id,
                                              uint32_t seq_num,
                                              uint32_t req_id,
                                              const UA_NodeId *node_id,
                                              const UA_Variant *value,
                                              uint8_t *tx_buf,
                                              size_t tx_cap);

/*
 * Encode an OPC UA Part 14 UADP Binary NetworkMessage (UDP PubSub telemetry).
 *
 * Wire layout (20 bytes total):
 *   [0]      VersionAndFlags  = 0x11 (UADP v1, PublisherId enabled)
 *   [1]      ExtendedFlags1   = 0x01 (PublisherId UInt16, DataSetPayloadHeader)
 *   [2..3]   PublisherId      = uint16_le (e.g., 0x000A = DE10-Nano Node 10)
 *   [4]      PayloadCount     = 0x01 (1 DataSetMessage)
 *   [5..6]   DataSetWriterId  = uint16_le (0x0001 = FPGA Telemetry Writer)
 *   [7]      DataSetFlags1    = 0x09 (Valid=1, Variant encoding, SeqNum enabled)
 *   [8..9]   SequenceNumber   = uint16_le
 *   [10..11] FieldCount       = uint16_le (3 fields)
 *   [12..13] Field 0: [UA_TYPE_BYTE (3)][uint8_t led_pattern]
 *   [14..18] Field 1: [UA_TYPE_UINT32 (7)][uint32_le op_count]
 *   [19..23] Field 2: [UA_TYPE_UINT32 (7)][uint32_le uptime_sec]
 *   Total = 24 bytes.
 */
size_t UA_PubSub_encodeUadpTelemetry(uint16_t publisher_id,
                                     uint16_t writer_id,
                                     uint16_t seq_num,
                                     uint8_t led_pattern,
                                     uint32_t op_count,
                                     uint32_t uptime_sec,
                                     uint8_t *tx_buf,
                                     size_t tx_cap);

#ifdef __cplusplus
}
#endif

#endif /* UA_MICRO_H */
