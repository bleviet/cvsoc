/*
 * ua_micro.c — Lightweight ANSI C99 OPC UA Binary (opc.tcp) & Part 14 UADP
 *              PubSub runtime for embedded FPGA+SoC targets.
 */

#include "ua_micro.h"
#include <string.h>
#include <stdio.h>

/* ── Little-Endian Binary Encoding / Decoding Helpers (IEC 62541-6 §5.2) ── */

static void put_u8(uint8_t *buf, size_t *pos, uint8_t v)
{
    buf[(*pos)++] = v;
}

static void put_u16_le(uint8_t *buf, size_t *pos, uint16_t v)
{
    buf[(*pos)++] = (uint8_t)(v & 0xFFu);
    buf[(*pos)++] = (uint8_t)((v >> 8) & 0xFFu);
}

static void put_u32_le(uint8_t *buf, size_t *pos, uint32_t v)
{
    buf[(*pos)++] = (uint8_t)(v & 0xFFu);
    buf[(*pos)++] = (uint8_t)((v >> 8) & 0xFFu);
    buf[(*pos)++] = (uint8_t)((v >> 16) & 0xFFu);
    buf[(*pos)++] = (uint8_t)((v >> 24) & 0xFFu);
}

static void put_string(uint8_t *buf, size_t *pos, const char *s)
{
    if (!s) {
        put_u32_le(buf, pos, 0xFFFFFFFFu); /* -1 = Null UA_String */
        return;
    }
    uint32_t len = (uint32_t)strlen(s);
    put_u32_le(buf, pos, len);
    if (len > 0) {
        memcpy(&buf[*pos], s, len);
        *pos += len;
    }
}

static void put_nodeid_string(uint8_t *buf, size_t *pos, const UA_NodeId *id)
{
    /* Encoding byte 0x03 = String NodeId: [0x03][uint16 ns][UA_String id] */
    put_u8(buf, pos, 0x03u);
    put_u16_le(buf, pos, id->namespace_index);
    put_string(buf, pos, id->identifier);
}

static void put_service_nodeid(uint8_t *buf, size_t *pos, uint16_t numeric_id)
{
    /* Encoding byte 0x01 = Four-Byte Numeric NodeId: [0x01][ns=0][uint16 id] */
    put_u8(buf, pos, 0x01u);
    put_u8(buf, pos, 0x00u);
    put_u16_le(buf, pos, numeric_id);
}

static void put_variant(uint8_t *buf, size_t *pos, const UA_Variant *v)
{
    put_u8(buf, pos, (uint8_t)v->type);
    switch (v->type) {
    case UA_TYPE_BOOLEAN:
        put_u8(buf, pos, v->value.b ? 1u : 0u);
        break;
    case UA_TYPE_BYTE:
        put_u8(buf, pos, v->value.u8);
        break;
    case UA_TYPE_UINT32:
        put_u32_le(buf, pos, v->value.u32);
        break;
    case UA_TYPE_STRING:
        put_string(buf, pos, v->value.str);
        break;
    default:
        put_u8(buf, pos, 0u);
        break;
    }
}

static bool get_u8(const uint8_t *buf, size_t len, size_t *pos, uint8_t *out)
{
    if (*pos + 1 > len) return false;
    *out = buf[(*pos)++];
    return true;
}

static bool get_u16_le(const uint8_t *buf, size_t len, size_t *pos, uint16_t *out)
{
    if (*pos + 2 > len) return false;
    *out = (uint16_t)buf[*pos] | ((uint16_t)buf[*pos + 1] << 8);
    *pos += 2;
    return true;
}

static bool get_u32_le(const uint8_t *buf, size_t len, size_t *pos, uint32_t *out)
{
    if (*pos + 4 > len) return false;
    *out = (uint32_t)buf[*pos]
         | ((uint32_t)buf[*pos + 1] << 8)
         | ((uint32_t)buf[*pos + 2] << 16)
         | ((uint32_t)buf[*pos + 3] << 24);
    *pos += 4;
    return true;
}

static bool get_string(const uint8_t *buf, size_t len, size_t *pos,
                       char *out, size_t out_cap)
{
    uint32_t slen = 0;
    if (!get_u32_le(buf, len, pos, &slen)) return false;
    if (slen == 0xFFFFFFFFu) {
        if (out_cap > 0) out[0] = '\0';
        return true;
    }
    if (*pos + slen > len) return false;
    size_t copy_len = (slen < out_cap - 1) ? slen : (out_cap - 1);
    memcpy(out, &buf[*pos], copy_len);
    out[copy_len] = '\0';
    *pos += slen;
    return true;
}

static bool get_nodeid_string(const uint8_t *buf, size_t len, size_t *pos,
                              UA_NodeId *out)
{
    uint8_t encoding = 0;
    if (!get_u8(buf, len, pos, &encoding)) return false;
    if (encoding == 0x03u) {
        if (!get_u16_le(buf, len, pos, &out->namespace_index)) return false;
        return get_string(buf, len, pos, out->identifier, sizeof(out->identifier));
    } else if (encoding == 0x01u) {
        uint8_t ns = 0;
        uint16_t num = 0;
        if (!get_u8(buf, len, pos, &ns)) return false;
        if (!get_u16_le(buf, len, pos, &num)) return false;
        out->namespace_index = ns;
        snprintf(out->identifier, sizeof(out->identifier), "i=%u", (unsigned)num);
        return true;
    }
    return false;
}

static bool get_variant(const uint8_t *buf, size_t len, size_t *pos,
                        UA_Variant *out)
{
    uint8_t t = 0;
    if (!get_u8(buf, len, pos, &t)) return false;
    out->type = (UA_BuiltinType)t;
    switch (out->type) {
    case UA_TYPE_BOOLEAN: {
        uint8_t b = 0;
        if (!get_u8(buf, len, pos, &b)) return false;
        out->value.b = (b != 0);
        return true;
    }
    case UA_TYPE_BYTE:
        return get_u8(buf, len, pos, &out->value.u8);
    case UA_TYPE_UINT32:
        return get_u32_le(buf, len, pos, &out->value.u32);
    case UA_TYPE_STRING:
        return get_string(buf, len, pos, out->value.str, sizeof(out->value.str));
    default:
        return false;
    }
}

/* ── Address Space Management ────────────────────────────────────────────── */

void UA_Server_init(UA_Server *server, const char *endpoint_url)
{
    memset(server, 0, sizeof(*server));
    if (endpoint_url) {
        strncpy(server->endpoint_url, endpoint_url, sizeof(server->endpoint_url) - 1);
    }
}

UA_NodeId UA_NODEID_STRING(uint16_t ns_idx, const char *id_str)
{
    UA_NodeId id;
    memset(&id, 0, sizeof(id));
    id.namespace_index = ns_idx;
    if (id_str) {
        strncpy(id.identifier, id_str, sizeof(id.identifier) - 1);
    }
    return id;
}

static UA_Node *alloc_node(UA_Server *server,
                           UA_NodeId node_id,
                           const char *parent_id,
                           const char *browse_name,
                           const char *display_name,
                           const char *description,
                           UA_NodeClass node_class)
{
    if (server->node_count >= UA_MAX_NODES) return NULL;
    UA_Node *n = &server->nodes[server->node_count++];
    memset(n, 0, sizeof(*n));
    n->node_id = node_id;
    if (parent_id)
        strncpy(n->parent_id, parent_id, sizeof(n->parent_id) - 1);
    if (browse_name)
        strncpy(n->browse_name, browse_name, sizeof(n->browse_name) - 1);
    if (display_name)
        strncpy(n->display_name, display_name, sizeof(n->display_name) - 1);
    if (description)
        strncpy(n->description, description, sizeof(n->description) - 1);
    n->node_class = node_class;
    return n;
}

UA_StatusCode UA_Server_addObjectNode(UA_Server *server,
                                      UA_NodeId node_id,
                                      const char *parent_id,
                                      const char *browse_name,
                                      const char *display_name,
                                      const char *description)
{
    UA_Node *n = alloc_node(server, node_id, parent_id, browse_name,
                            display_name, description, UA_NODECLASS_OBJECT);
    return n ? UA_STATUSCODE_GOOD : UA_STATUSCODE_BADINTERNALERROR;
}

UA_StatusCode UA_Server_addDataSourceVariableNode(UA_Server *server,
                                                  UA_NodeId node_id,
                                                  const char *parent_id,
                                                  const char *browse_name,
                                                  const char *display_name,
                                                  const char *description,
                                                  UA_BuiltinType data_type,
                                                  uint8_t access_level,
                                                  UA_DataSource data_source,
                                                  void *user_context)
{
    UA_Node *n = alloc_node(server, node_id, parent_id, browse_name,
                            display_name, description, UA_NODECLASS_VARIABLE);
    if (!n) return UA_STATUSCODE_BADINTERNALERROR;
    n->data_type    = data_type;
    n->access_level = access_level;
    n->data_source  = data_source;
    n->user_context = user_context;
    return UA_STATUSCODE_GOOD;
}

UA_StatusCode UA_Server_addMethodNode(UA_Server *server,
                                      UA_NodeId node_id,
                                      const char *parent_id,
                                      const char *browse_name,
                                      const char *display_name,
                                      const char *description,
                                      UA_MethodCallback method_cb,
                                      void *user_context)
{
    UA_Node *n = alloc_node(server, node_id, parent_id, browse_name,
                            display_name, description, UA_NODECLASS_METHOD);
    if (!n) return UA_STATUSCODE_BADINTERNALERROR;
    n->access_level = UA_ACCESSLEVELMASK_READ;
    n->method_cb    = method_cb;
    n->user_context = user_context;
    return UA_STATUSCODE_GOOD;
}

const UA_Node *UA_Server_findNode(const UA_Server *server,
                                  uint16_t ns_idx,
                                  const char *identifier)
{
    for (size_t i = 0; i < server->node_count; i++) {
        if (server->nodes[i].node_id.namespace_index == ns_idx &&
            strcmp(server->nodes[i].node_id.identifier, identifier) == 0) {
            return &server->nodes[i];
        }
    }
    return NULL;
}

/* ── OPC UA TCP Chunk Header Patching ────────────────────────────────────── */

static void finalize_chunk_header(uint8_t *tx_buf, const char type[4], size_t total_len)
{
    memcpy(tx_buf, type, 4);
    size_t pos = 4;
    put_u32_le(tx_buf, &pos, (uint32_t)total_len);
}

/* ── Service Handlers (Browse, Read, Write, Call, Subscribe) ─────────────── */

static int handle_msg_chunk(UA_Server *server,
                            const uint8_t *rx_buf,
                            size_t rx_len,
                            uint8_t *tx_buf,
                            size_t tx_cap)
{
    /*
     * MSGF Secure Conversation Message Header (24 bytes, IEC 62541-6 §6.7.2):
     *   [0..3]   "MSGF"
     *   [4..7]   MessageSize (uint32_le)
     *   [8..11]  SecureChannelId (uint32_le)
     *   [12..15] TokenId (uint32_le)
     *   [16..19] SequenceNumber (uint32_le)
     *   [20..23] RequestId (uint32_le)
     *   [24..27] Service NodeId: [0x01][0x00][uint16_le service_id]
     */
    if (rx_len < 28 || tx_cap < 512) return -1;

    size_t rpos = 8;
    uint32_t sec_channel_id = 0, token_id = 0, seq_num = 0, req_id = 0;
    get_u32_le(rx_buf, rx_len, &rpos, &sec_channel_id);
    get_u32_le(rx_buf, rx_len, &rpos, &token_id);
    get_u32_le(rx_buf, rx_len, &rpos, &seq_num);
    get_u32_le(rx_buf, rx_len, &rpos, &req_id);

    uint8_t enc_type = 0, ns_zero = 0;
    uint16_t svc_id = 0;
    if (!get_u8(rx_buf, rx_len, &rpos, &enc_type) ||
        !get_u8(rx_buf, rx_len, &rpos, &ns_zero) ||
        !get_u16_le(rx_buf, rx_len, &rpos, &svc_id) ||
        enc_type != 0x01u) {
        return -1;
    }

    /* Prepare 24-byte response header */
    size_t wpos = 8;
    put_u32_le(tx_buf, &wpos, sec_channel_id);
    put_u32_le(tx_buf, &wpos, token_id);
    put_u32_le(tx_buf, &wpos, seq_num);
    put_u32_le(tx_buf, &wpos, req_id);

    switch (svc_id) {
    case UA_SVC_BROWSE_REQ: {
        /*
         * BrowseRequest:
         *   parent_filter: UA_String ("" or "Objects" returns all nodes,
         *                  otherwise filters by parent_id)
         */
        char parent_filter[UA_MAX_STRING_LEN] = "";
        if (rpos < rx_len) {
            get_string(rx_buf, rx_len, &rpos, parent_filter, sizeof(parent_filter));
        }

        put_service_nodeid(tx_buf, &wpos, UA_SVC_BROWSE_RESP);
        put_u32_le(tx_buf, &wpos, UA_STATUSCODE_GOOD);

        /* Count matching nodes */
        uint32_t match_count = 0;
        for (size_t i = 0; i < server->node_count; i++) {
            if (parent_filter[0] == '\0' ||
                strcmp(parent_filter, "Objects") == 0 ||
                strcmp(server->nodes[i].parent_id, parent_filter) == 0) {
                match_count++;
            }
        }
        put_u32_le(tx_buf, &wpos, match_count);

        for (size_t i = 0; i < server->node_count; i++) {
            const UA_Node *n = &server->nodes[i];
            if (parent_filter[0] != '\0' &&
                strcmp(parent_filter, "Objects") != 0 &&
                strcmp(n->parent_id, parent_filter) != 0) {
                continue;
            }
            put_nodeid_string(tx_buf, &wpos, &n->node_id);
            put_string(tx_buf, &wpos, n->parent_id);
            put_string(tx_buf, &wpos, n->browse_name);
            put_string(tx_buf, &wpos, n->display_name);
            put_string(tx_buf, &wpos, n->description);
            put_u8(tx_buf, &wpos, (uint8_t)n->node_class);
            put_u8(tx_buf, &wpos, (uint8_t)n->data_type);
            put_u8(tx_buf, &wpos, n->access_level);

            /* For Variable nodes, include current value snapshot */
            if (n->node_class == UA_NODECLASS_VARIABLE && n->data_source.read) {
                UA_Variant val;
                memset(&val, 0, sizeof(val));
                val.type = n->data_type;
                n->data_source.read(n->user_context, &n->node_id, &val);
                put_variant(tx_buf, &wpos, &val);
            } else {
                UA_Variant empty;
                memset(&empty, 0, sizeof(empty));
                empty.type = UA_TYPE_STRING;
                put_variant(tx_buf, &wpos, &empty);
            }
        }
        break;
    }

    case UA_SVC_READ_REQ: {
        UA_NodeId target_id;
        memset(&target_id, 0, sizeof(target_id));
        put_service_nodeid(tx_buf, &wpos, UA_SVC_READ_RESP);

        if (!get_nodeid_string(rx_buf, rx_len, &rpos, &target_id)) {
            put_u32_le(tx_buf, &wpos, UA_STATUSCODE_BADDECODINGERROR);
            break;
        }

        const UA_Node *n = UA_Server_findNode(server, target_id.namespace_index,
                                              target_id.identifier);
        if (!n) {
            put_u32_le(tx_buf, &wpos, UA_STATUSCODE_BADNODEIDUNKNOWN);
            break;
        }
        if (n->node_class != UA_NODECLASS_VARIABLE || !n->data_source.read) {
            put_u32_le(tx_buf, &wpos, UA_STATUSCODE_BADNOTREADABLE);
            break;
        }

        UA_Variant val;
        memset(&val, 0, sizeof(val));
        val.type = n->data_type;
        UA_StatusCode st = n->data_source.read(n->user_context, &n->node_id, &val);
        put_u32_le(tx_buf, &wpos, st);
        if (st == UA_STATUSCODE_GOOD) {
            put_variant(tx_buf, &wpos, &val);
        }
        break;
    }

    case UA_SVC_WRITE_REQ: {
        UA_NodeId target_id;
        UA_Variant in_val;
        memset(&target_id, 0, sizeof(target_id));
        memset(&in_val, 0, sizeof(in_val));
        put_service_nodeid(tx_buf, &wpos, UA_SVC_WRITE_RESP);

        if (!get_nodeid_string(rx_buf, rx_len, &rpos, &target_id) ||
            !get_variant(rx_buf, rx_len, &rpos, &in_val)) {
            put_u32_le(tx_buf, &wpos, UA_STATUSCODE_BADDECODINGERROR);
            break;
        }

        const UA_Node *n = UA_Server_findNode(server, target_id.namespace_index,
                                              target_id.identifier);
        if (!n) {
            put_u32_le(tx_buf, &wpos, UA_STATUSCODE_BADNODEIDUNKNOWN);
            break;
        }
        if (!(n->access_level & UA_ACCESSLEVELMASK_WRITE) || !n->data_source.write) {
            put_u32_le(tx_buf, &wpos, UA_STATUSCODE_BADNOTWRITABLE);
            break;
        }
        if (in_val.type != n->data_type) {
            put_u32_le(tx_buf, &wpos, UA_STATUSCODE_BADTYPEMISMATCH);
            break;
        }

        UA_StatusCode st = n->data_source.write(n->user_context, &n->node_id, &in_val);
        put_u32_le(tx_buf, &wpos, st);
        break;
    }

    case UA_SVC_CALL_REQ: {
        UA_NodeId method_id;
        UA_Variant in_arg;
        memset(&method_id, 0, sizeof(method_id));
        memset(&in_arg, 0, sizeof(in_arg));
        put_service_nodeid(tx_buf, &wpos, UA_SVC_CALL_RESP);

        if (!get_nodeid_string(rx_buf, rx_len, &rpos, &method_id)) {
            put_u32_le(tx_buf, &wpos, UA_STATUSCODE_BADDECODINGERROR);
            break;
        }
        /* Optional input argument */
        if (rpos < rx_len) {
            get_variant(rx_buf, rx_len, &rpos, &in_arg);
        }

        const UA_Node *n = UA_Server_findNode(server, method_id.namespace_index,
                                              method_id.identifier);
        if (!n) {
            put_u32_le(tx_buf, &wpos, UA_STATUSCODE_BADNODEIDUNKNOWN);
            break;
        }
        if (n->node_class != UA_NODECLASS_METHOD || !n->method_cb) {
            put_u32_le(tx_buf, &wpos, UA_STATUSCODE_BADMETHODINVALID);
            break;
        }

        UA_Variant out_arg;
        memset(&out_arg, 0, sizeof(out_arg));
        out_arg.type = UA_TYPE_BYTE;
        UA_StatusCode st = n->method_cb(n->user_context, &n->node_id, &in_arg, &out_arg);
        put_u32_le(tx_buf, &wpos, st);
        if (st == UA_STATUSCODE_GOOD) {
            put_variant(tx_buf, &wpos, &out_arg);
        }
        break;
    }

    case UA_SVC_CREATESUB_REQ: {
        UA_NodeId mon_id;
        memset(&mon_id, 0, sizeof(mon_id));
        put_service_nodeid(tx_buf, &wpos, UA_SVC_CREATESUB_RESP);

        if (!get_nodeid_string(rx_buf, rx_len, &rpos, &mon_id)) {
            put_u32_le(tx_buf, &wpos, UA_STATUSCODE_BADDECODINGERROR);
            break;
        }
        const UA_Node *n = UA_Server_findNode(server, mon_id.namespace_index,
                                              mon_id.identifier);
        if (!n) {
            put_u32_le(tx_buf, &wpos, UA_STATUSCODE_BADNODEIDUNKNOWN);
            break;
        }
        put_u32_le(tx_buf, &wpos, UA_STATUSCODE_GOOD);
        put_u32_le(tx_buf, &wpos, 1u); /* SubscriptionId = 1 */
        break;
    }

    default:
        put_service_nodeid(tx_buf, &wpos, svc_id);
        put_u32_le(tx_buf, &wpos, UA_STATUSCODE_BADSERVICEUNSUPPORTED);
        break;
    }

    finalize_chunk_header(tx_buf, "MSGF", wpos);
    return (int)wpos;
}

int UA_Server_processChunk(UA_Server *server,
                           const uint8_t *rx_buf,
                           size_t rx_len,
                           uint8_t *tx_buf,
                           size_t tx_cap,
                           bool *out_should_close)
{
    if (out_should_close) *out_should_close = false;
    if (rx_len < 8 || tx_cap < 64) return -1;

    /* 1. HELF -> ACKF (28 bytes, IEC 62541-6 §7.1.2.4) */
    if (memcmp(rx_buf, "HELF", 4) == 0) {
        size_t wpos = 8;
        put_u32_le(tx_buf, &wpos, 0u);     /* ProtocolVersion = 0 */
        put_u32_le(tx_buf, &wpos, 65536u); /* ReceiveBufferSize */
        put_u32_le(tx_buf, &wpos, 65536u); /* SendBufferSize */
        put_u32_le(tx_buf, &wpos, 65536u); /* MaxMessageSize */
        put_u32_le(tx_buf, &wpos, 1u);     /* MaxChunkCount */
        finalize_chunk_header(tx_buf, "ACKF", wpos);
        return (int)wpos;
    }

    /* 2. OPNF -> OPNF (OpenSecureChannel response) */
    if (memcmp(rx_buf, "OPNF", 4) == 0) {
        size_t wpos = 8;
        put_u32_le(tx_buf, &wpos, 1u);             /* SecureChannelId = 1 */
        put_u32_le(tx_buf, &wpos, 1u);             /* TokenId = 1 */
        put_u32_le(tx_buf, &wpos, 3600000u);       /* RevisedLifetime = 3600s */
        put_string(tx_buf, &wpos, server->endpoint_url);
        finalize_chunk_header(tx_buf, "OPNF", wpos);
        return (int)wpos;
    }

    /* 3. MSGF -> MSGF (Service Call) */
    if (memcmp(rx_buf, "MSGF", 4) == 0) {
        return handle_msg_chunk(server, rx_buf, rx_len, tx_buf, tx_cap);
    }

    /* 4. CLOF -> Close connection */
    if (memcmp(rx_buf, "CLOF", 4) == 0) {
        if (out_should_close) *out_should_close = true;
        return 0;
    }

    return -1;
}

size_t UA_Server_encodeDataChangeNotification(uint32_t secure_channel_id,
                                              uint32_t token_id,
                                              uint32_t seq_num,
                                              uint32_t req_id,
                                              const UA_NodeId *node_id,
                                              const UA_Variant *value,
                                              uint8_t *tx_buf,
                                              size_t tx_cap)
{
    if (tx_cap < 128) return 0;
    size_t wpos = 8;
    put_u32_le(tx_buf, &wpos, secure_channel_id);
    put_u32_le(tx_buf, &wpos, token_id);
    put_u32_le(tx_buf, &wpos, seq_num);
    put_u32_le(tx_buf, &wpos, req_id);
    put_service_nodeid(tx_buf, &wpos, UA_SVC_PUBLISH_RESP);
    put_u32_le(tx_buf, &wpos, UA_STATUSCODE_GOOD);
    put_nodeid_string(tx_buf, &wpos, node_id);
    put_variant(tx_buf, &wpos, value);
    finalize_chunk_header(tx_buf, "MSGF", wpos);
    return wpos;
}

size_t UA_PubSub_encodeUadpTelemetry(uint16_t publisher_id,
                                     uint16_t writer_id,
                                     uint16_t seq_num,
                                     uint8_t led_pattern,
                                     uint32_t op_count,
                                     uint32_t uptime_sec,
                                     uint8_t *tx_buf,
                                     size_t tx_cap)
{
    if (tx_cap < 32) return 0;
    size_t pos = 0;

    /* UADP NetworkMessage Header (IEC 62541-14 §7.2.2) */
    put_u8(tx_buf, &pos, 0x11u);         /* Version=1, PublisherId=1, ExtendedFlags1=1 */
    put_u8(tx_buf, &pos, 0x01u);         /* PublisherId Type = UInt16 */
    put_u16_le(tx_buf, &pos, publisher_id);

    /* DataSetPayloadHeader */
    put_u8(tx_buf, &pos, 1u);            /* 1 DataSetMessage */
    put_u16_le(tx_buf, &pos, writer_id); /* DataSetWriterId */

    /* DataSetMessage Header */
    put_u8(tx_buf, &pos, 0x09u);         /* Valid=1, Variant encoding, SeqNum enabled */
    put_u16_le(tx_buf, &pos, seq_num);   /* SequenceNumber */
    put_u16_le(tx_buf, &pos, 3u);        /* FieldCount = 3 */

    /* Field 0: LedPattern (Byte) */
    put_u8(tx_buf, &pos, (uint8_t)UA_TYPE_BYTE);
    put_u8(tx_buf, &pos, led_pattern);

    /* Field 1: OperationCount (UInt32) */
    put_u8(tx_buf, &pos, (uint8_t)UA_TYPE_UINT32);
    put_u32_le(tx_buf, &pos, op_count);

    /* Field 2: UptimeSeconds (UInt32) */
    put_u8(tx_buf, &pos, (uint8_t)UA_TYPE_UINT32);
    put_u32_le(tx_buf, &pos, uptime_sec);

    return pos; /* 24 bytes total */
}
