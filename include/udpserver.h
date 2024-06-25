#ifndef __UDPSERVER_H__
#define __UDPSERVER_H__

#include "selector.h"

void udp_read_handler(struct selector_key *key);

#endif
