/*
 * gev.h — GigE Vision protocol constants used by camd.
 *
 * Bit positions follow the integer values on the wire. The GigE Vision
 * specification numbers bits MSB-first (bit 0 = 0x80000000); comments give
 * the spec's name where it helps cross-referencing.
 */
#ifndef GEV_H
#define GEV_H

#include <stdint.h>

/* ── GVCP (control protocol, UDP) ─────────────────────────────────────────── */
#define GVCP_PORT               3956
#define GVCP_KEY                0x42
#define GVCP_HDR_LEN            8
#define GVCP_MAX_PACKET         576
#define GVCP_MAX_READMEM        536     /* data bytes per READMEM */
#define GVCP_MAX_REGS           135     /* registers per READREG / WRITEREG */

#define GVCP_FLAG_ACK_REQUIRED  0x01
#define GVCP_FLAG_ALLOW_BCAST   0x10    /* discovery: broadcast ack allowed */

#define GVCP_DISCOVERY_CMD      0x0002
#define GVCP_DISCOVERY_ACK      0x0003
#define GVCP_FORCEIP_CMD        0x0004
#define GVCP_FORCEIP_ACK        0x0005
#define GVCP_PACKETRESEND_CMD   0x0040
#define GVCP_READREG_CMD        0x0080
#define GVCP_READREG_ACK        0x0081
#define GVCP_WRITEREG_CMD       0x0082
#define GVCP_WRITEREG_ACK       0x0083
#define GVCP_READMEM_CMD        0x0084
#define GVCP_READMEM_ACK        0x0085
#define GVCP_WRITEMEM_CMD       0x0086
#define GVCP_WRITEMEM_ACK       0x0087

#define GEV_STATUS_SUCCESS          0x0000
#define GEV_STATUS_NOT_IMPLEMENTED  0x8001
#define GEV_STATUS_INVALID_PARAMETER 0x8002
#define GEV_STATUS_INVALID_ADDRESS  0x8003
#define GEV_STATUS_WRITE_PROTECT    0x8004
#define GEV_STATUS_BAD_ALIGNMENT    0x8005
#define GEV_STATUS_ACCESS_DENIED    0x8006
#define GEV_STATUS_BUSY             0x8007

/* ── Bootstrap registers ──────────────────────────────────────────────────── */
#define BS_SIZE                 0x10000
#define BS_VERSION              0x0000
#define BS_DEVICE_MODE          0x0004
#define BS_MAC_HIGH             0x0008
#define BS_MAC_LOW              0x000C
#define BS_NET_CAPABILITY       0x0010
#define BS_NET_CONFIG           0x0014
#define BS_CURRENT_IP           0x0024
#define BS_CURRENT_MASK         0x0034
#define BS_CURRENT_GATEWAY      0x0044
#define BS_MANUFACTURER_NAME    0x0048  /* 32 bytes */
#define BS_MODEL_NAME           0x0068  /* 32 bytes */
#define BS_DEVICE_VERSION       0x0088  /* 32 bytes */
#define BS_MANUFACTURER_INFO    0x00A8  /* 48 bytes */
#define BS_SERIAL_NUMBER        0x00D8  /* 16 bytes */
#define BS_USER_NAME            0x00E8  /* 16 bytes, writable */
#define BS_DISCOVERY_DATA_LEN   0x00F8  /* discovery ack = bootstrap[0, 0xF8) */
#define BS_FIRST_URL            0x0200  /* 512 bytes */
#define BS_SECOND_URL           0x0400  /* 512 bytes */
#define BS_NUM_NET_INTERFACES   0x0600
#define BS_PERSISTENT_IP        0x064C
#define BS_PERSISTENT_MASK      0x065C
#define BS_PERSISTENT_GATEWAY   0x066C
#define BS_NUM_MESSAGE_CHANNELS 0x0900
#define BS_NUM_STREAM_CHANNELS  0x0904
#define BS_GVCP_CAPABILITY      0x0934
#define BS_HEARTBEAT_TIMEOUT    0x0938
#define BS_TS_FREQ_HIGH         0x093C
#define BS_TS_FREQ_LOW          0x0940
#define BS_TS_CONTROL           0x0944
#define BS_TS_VALUE_HIGH        0x0948
#define BS_TS_VALUE_LOW         0x094C
#define BS_DISCOVERY_ACK_DELAY  0x0950
#define BS_GVCP_CONFIG          0x0954
#define BS_PENDING_TIMEOUT      0x0958
#define BS_CCP                  0x0A00
#define BS_MCP                  0x0B00
#define BS_MCDA                 0x0B10
#define BS_MCTT                 0x0B14
#define BS_MCRC                 0x0B18
#define BS_SCP0                 0x0D00
#define BS_SCPS0                0x0D04
#define BS_SCPD0                0x0D08
#define BS_SCDA0                0x0D18
#define BS_SCSP0                0x0D1C
#define BS_SCC0                 0x0D20
#define BS_SCCFG0               0x0D24

#define DEVICE_MODE_BIG_ENDIAN  0x80000000u  /* spec bit 0 "E" */
#define DEVICE_MODE_UTF8        0x00000001u

#define NET_CAP_PERSISTENT_IP   0x00000001u
#define NET_CAP_DHCP            0x00000002u
#define NET_CAP_LLA             0x00000004u

#define GVCP_CAP_USER_NAME      0x80000000u  /* spec bit 0 */
#define GVCP_CAP_SERIAL_NUMBER  0x40000000u  /* spec bit 1 */
#define GVCP_CAP_HEARTBEAT_DIS  0x20000000u  /* spec bit 2 */
#define GVCP_CAP_WRITEMEM       0x00000002u  /* spec bit 30 */
#define GVCP_CAP_CONCATENATION  0x00000001u  /* spec bit 31 */

#define GVCP_CONFIG_HEARTBEAT_DISABLE 0x00000001u

#define CCP_EXCLUSIVE           0x00000001u
#define CCP_CONTROL             0x00000002u

#define TS_CONTROL_RESET        0x00000001u
#define TS_CONTROL_LATCH        0x00000002u

#define SCPS_FIRE_TEST_PACKET   0x80000000u  /* spec bit 0 "F" */
#define SCPS_DO_NOT_FRAGMENT    0x40000000u  /* spec bit 1 "D" */
#define SCPS_SIZE_MASK          0x0000FFFFu

/* ── GVSP (stream protocol, UDP) ──────────────────────────────────────────── */
#define GVSP_HDR_LEN            8
#define GVSP_IP_UDP_OVERHEAD    28            /* IPv4 20 + UDP 8 */
#define GVSP_PACKET_OVERHEAD    (GVSP_IP_UDP_OVERHEAD + GVSP_HDR_LEN)

#define GVSP_FORMAT_LEADER      1
#define GVSP_FORMAT_TRAILER     2
#define GVSP_FORMAT_PAYLOAD     3

#define GVSP_PAYLOAD_TYPE_IMAGE 0x0001

/* ── PFNC pixel formats ───────────────────────────────────────────────────── */
#define PFNC_MONO8              0x01080001u
#define PFNC_BAYER_GR8          0x01080008u

#endif /* GEV_H */
