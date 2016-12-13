#ifndef _SYNCRELATION_H_
#define _SYNCRELATION_H_

#include <fuse.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "deltacfs_client_gearman.h"

void rel_enqueue(char *src, char *dst, time_t localtime);

void rel_dequeue();

rel_hash* rel_find(char *src);

void rel_delete(rel_hash *r);

void rel_delete_src(char *src);

void rel_delete_dst(char *dst);

void rel_print();

#endif
