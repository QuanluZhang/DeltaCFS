#include "synctimestamp.h"
#include <assert.h>
#include <string.h>

int recfs_update_timestamp(leveldb_t *db, const char *path, const uint64_t cnt)
{
	char *err = NULL;
	uint32_t pathlen = strlen(path);

	leveldb_writeoptions_t *woptions;
	woptions = leveldb_writeoptions_create();
	leveldb_put(db, woptions, path, pathlen, (const char*)&cnt, sizeof(cnt), &err);
	if (err != NULL) {
		fprintf(stderr, "recfs_update_timestamp leveldb_put error!\n");
		leveldb_free(err);
		err = NULL;
		return -1;
	}
	return 0;
}

int recfs_get_timestamp(leveldb_t *db, const char *path, uint64_t *cnt)
{
	char *cnt_ptr = NULL;
	char *err = NULL;
	size_t readlen;
	uint32_t pathlen = strlen(path);

	leveldb_readoptions_t *roptions;
	roptions = leveldb_readoptions_create();

	cnt_ptr = leveldb_get(db, roptions, path, pathlen, &readlen, &err);
	if (err != NULL) {
		fprintf(stderr, "recfs_get_timestamp leveldb_get error!\n");
		leveldb_free(err);
		err = NULL;
		return -1;
	}
    if (cnt_ptr == NULL) {
        fprintf(stderr, "recfs_get_timestamp leveldb_get null %s\n",path);
    }
	assert(cnt_ptr != NULL);
	*cnt = *(uint64_t *)cnt_ptr;
	return 0;
}

int recfs_del_timestamp(leveldb_t *db, const char *path)
{
	char *err = NULL;
	uint32_t pathlen = strlen(path);

	leveldb_writeoptions_t *woptions;
	woptions = leveldb_writeoptions_create();

	leveldb_delete(db, woptions, path, pathlen, &err);
	if (err != NULL) {
		fprintf(stderr, "recfs_del_timestamp leveldb_delete error!\n");
		leveldb_free(err);
		err = NULL;
		return -1;
	}
	return 0;
}
