/*
 * gvcp.h — GVCP command handling (discovery, register and memory access).
 */
#ifndef GVCP_H
#define GVCP_H

#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>

#include "camera.h"

int  gvcp_open(uint16_t port);
/* Handle one command datagram from `peer` and send its ack on `fd`. */
void gvcp_handle(struct camera *c, int fd, const uint8_t *pkt, size_t len,
                 const struct sockaddr_in *peer, int verbose);

#endif /* GVCP_H */
