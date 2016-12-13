#ifndef _SYNCCHECKSUM_H_
#define _SYNCCHECKSUM_H_

#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <leveldb/c.h>
#include "librsync.h"
#include "checksum.h"
#include "deltacfs_client_gearman.h"
#include <assert.h>

void checksum_checksync();

void checksum_update(int fd,const char *path, const char *buf, size_t size, off_t offset);

void checksum_enlarge(const char *path, off_t oldsize, off_t newsize);

void checksum_shrink(const char *path, off_t newsize);

int checksum_check(int fd,const char *path, char *buf, size_t size, off_t offset);

#endif
