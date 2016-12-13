#ifndef _SYNCQUEUE_H_
#define _SYNCQUEUE_H_

#include <fuse.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <leveldb/c.h>
#include "deltacfs_client_gearman.h"

void sync_enqueue(int op, const char *src, char *dst, uint64_t *para, buf_node *bn, char *file_buf, time_t localtime, uint64_t base_version);

sync_node* sync_dequeue();

void sync_print();

void sync_delete(const char *path);

sync_hash* sync_find(const char *path);

//called by: sync_execute xmp_mknod xmp_unlink xmp_rename xmp_link xmp_truncate 
void sync_pack_write(sync_node *this_sn); // TODO at least realize in sync_dequeue, rename, unlink

void sync_pack_write_path(const char *path);

void sync_delete_write(const char *path); // used on truncate 0 and delete write buffer

#endif
