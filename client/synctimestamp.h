#ifndef _SYNCTIMESTAMP_H_
#define _SYNCTIMESTAMP_H_

#include <leveldb/c.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>

/*
 * Write the file's timestamp to leveldb
 */
inline
int recfs_update_timestamp(leveldb_t *db, const char *path, uint64_t cnt);

/*
 * Get the file's timestamp from leveldb
 */
inline
int recfs_get_timestamp(leveldb_t *db, const char *path, uint64_t *cnt);

/*
 * Delete the file's timestamp from leveldb
 */
inline
int recfs_del_timestamp(leveldb_t *db, const char *path);

#endif
