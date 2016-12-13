#ifndef _SYNCSEND_H_
#define _SYNCSEND_H_

#include <fuse.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <unistd.h>
#include <libgearman/gearman.h>
#include <assert.h>
#include "deltacfs_client_gearman.h"

// NOTE: we think the maximum pathlen is 256 bytes
#define BLOBSIZE 1024
int sync_send(sync_node *sn);

#endif
