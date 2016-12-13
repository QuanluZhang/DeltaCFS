/*
  FUSE: Filesystem in Userspace
  Copyright (C) 

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

  gcc -Wall `pkg-config fuse --cflags --libs` fusexmp.c -o fusexmp
*/
/*
   stable version 1.0
   modify data structure to circular queue
*/

#define FUSE_USE_VERSION 26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#endif

#include <fuse.h>
#include "deltacfs_client_gearman.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#include <stdbool.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#include <time.h>
#endif
#include <pthread.h>
#include <librsync.h>
#include <sys/types.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <ctype.h>
#include <assert.h>
#include <sys/stat.h>
#include "syncdelta.h"
#include "syncqueue.h"
#include "syncrelation.h"
#include "syncsend.h"
#include "syncutil.h"
#include "syncchecksum.h"
#include "synctimestamp.h"

// define const path
char TMP_DIR[30] = "/tmp/cloudsynctmp/";
char TMP_SIG[30] = "/tmp/cloudsynctmp/sig_";
char TMP_DELTA[30] = "/tmp/cloudsynctmp/delta_";
char DB_PATH[30] = "/tmp/cloudsyncclientdb";

// declaration of data structure globle variable
rel_queue *rq;
sync_queue *sq;
rel_hash *rhs;
sync_hash *shs;
//newfile_hash *nhs;
leveldb_t *db;

gearman_client_st *gear_client;

// initial data structure
void initial()
{
	int i;
	printf("----------init global timestamp-------\n");
	recfs_cnt = 1;
    printf("-------------init rel_queue-----------\n");
	rq = (rel_queue*) malloc (sizeof(rel_queue));
	rq->head = 0;
	rq->tail = 0;
	for(i = 0; i < QUEUE_SIZE; i++)
	{
		rq->node[i].isEmpty = 1;
	}
	printf("-------------init sync_queue-----------\n");
	sq = (sync_queue*) malloc (sizeof(sync_queue));
	sq->head = 0;
	sq->tail = 0;
	for(i = 0; i < QUEUE_SIZE; i++)
	{
		sq->node[i].isEmpty = 1;
        pthread_mutex_init(&sq->node[i].mutex,NULL);
	}
    pthread_rwlock_init(&sq->hash_rwlock,NULL);
    sq->hash_cache_result=NULL;
	srand(time(0)); // for delta file name

	printf("-------------init leveldb-----------\n");
    leveldb_options_t *dboptions;
    char *err=NULL;
    dboptions=leveldb_options_create();
    leveldb_options_set_create_if_missing(dboptions,1);
    db=leveldb_open(dboptions,DB_PATH,&err);
    if (err != NULL) {
        fprintf(stderr,"initial leveldb_open() error\n");
    } else {
        fprintf(stderr,"initial leveldb_open() success\n");
    }
    leveldb_free(err); err = NULL;

	printf("-------------init gearman connection-----------\n");
	gear_client = gearman_client_create(NULL);
	gearman_return_t ret = gearman_client_add_server(gear_client, GEARMANIP, GEARMANPORT);
	if (gearman_failed(ret)) {
		fprintf(stderr, "Gearman client create failed!\n");
		//return -1;
		exit(0);
	}
	else {
		printf("Gearman client creation success!\n");
	}
}

// relation delay delete thread
// TODO: the corresponding file in tmp directory should be deleted with the deletion of the relation
void rel_execute()
{
	int i;
	int rn_record_tail;
    while(1)
    {
		/* record queue tail */
		rn_record_tail = rq->tail;
		rn_record_tail = (rn_record_tail >= rq->head) ? rn_record_tail:(rn_record_tail+QUEUE_SIZE);
		
		/* dequeue delay */
		sleep(REL_BATCHTIME);

		for (i = rq->head; i < rn_record_tail; i++) {
			assert(rq->node[i%QUEUE_SIZE].isEmpty != 1);
            pthread_rwlock_wrlock(&rq->hash_rwlock);
			//printf("\n<-- delay dequeue a relation -->\n");
			rel_dequeue();
            pthread_rwlock_unlock(&rq->hash_rwlock);
		}
    }
}

// sync execute function
void sync_execute()
{
	int i;
	int sn_record_tail;
	sync_node *sn;

	while(1)
	{
		sn_record_tail = sq->tail;
		sn_record_tail = (sn_record_tail >= sq->head) ? sn_record_tail:(sn_record_tail+QUEUE_SIZE);

		/* dequeue delay */
		sleep(SYNC_BATCHTIME);

		for (i = sq->head; i < sn_record_tail; i++) {
			assert(sq->node[i%QUEUE_SIZE].isEmpty != 1);
			sn = &sq->node[i%QUEUE_SIZE];

			pthread_rwlock_wrlock(&sq->hash_rwlock);
			pthread_mutex_lock(&sn->mutex); //acquires mutex lock

			//printf("\n<-- delay execute sync -->\n");
			//printf("op = %s\n", dict(sn->op));
			//printf("isDelete = %d\nisPacked = %d\n", sn->isDelete, sn->isPacked);
			if( (sn->op == NEWFILE || sn->op == WRITE/* || sn->op == DELTA*/) && sn->isDelete != 1 && sn->isPacked != 1) 
			{
				//printf("pack write\n");
				//pthread_mutex_unlock(&sn->mutex); //unlock it, TODO: may have problem
				sync_hash *sh = sync_find((char*)sn->src);
				if (sh) {
					HASH_DEL(shs, sh);
					free(sh);
					sq->hash_cache_result=NULL;
				}
				sync_pack_write(sn);
			} else {
				//printf("not pack\n");
			}

			pthread_rwlock_unlock(&sq->hash_rwlock);
			if(sn->isDelete != 1) {
				//sync_send(sn);
			}
			sync_dequeue();
			pthread_mutex_unlock(&sn->mutex);
		}
	}
}

// fuse operation
static int xmp_getattr(const char *path, struct stat *stbuf)
{
	int res;

	res = lstat(path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_access(const char *path, int mask)
{
	int res;

	res = access(path, mask);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_readlink(const char *path, char *buf, size_t size)
{
	int res;

	res = readlink(path, buf, size - 1);
	if (res == -1)
		return -errno;

	buf[res] = '\0';
	return 0;
}


static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
	DIR *dp;
	struct dirent *de;

	(void) offset;
	(void) fi;

	dp = opendir(path);
	if (dp == NULL)
		return -errno;

	while ((de = readdir(dp)) != NULL) {
		struct stat st;
		memset(&st, 0, sizeof(st));
		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12;
		if (filler(buf, de->d_name, &st, 0))
			break;
	}

	closedir(dp);
	return 0;
}

static int xmp_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int res;
	uint64_t new_timestamp;

	new_timestamp = INCREASE_RECFS_CNT;

	/* On Linux this could just be 'mknod(path, mode, rdev)' but this
	   is more portable */
	recfs_update_timestamp(db, path, new_timestamp);
	if (S_ISREG(mode)) {
		res = open(path, O_CREAT | O_EXCL | O_WRONLY, mode);
		if (res >= 0)
			res = close(res);
	} else if (S_ISFIFO(mode))
		res = mkfifo(path, mode);
	else {

		res = mknod(path, mode, rdev);
	}
	if (res == -1)
		return -errno;

	
	uint64_t para[10] = {0};
	para[1] = (int)mode;
	para[2] = (int)rdev;

	// add to sync queue
    sync_pack_write_path(path);
	sync_enqueue(NEWFILE, (char*)path, "", para, NULL, NULL, new_timestamp, 0);
	return 0;
}

/*
 * Corresponding to syncsend.c, we do not upload mkdir,
 * thus, sync_enqueue can be deleted.
 * TODO: mkdir should be uploaded as a file.
 */
static int xmp_mkdir(const char *path, mode_t mode)
{
	int res;
	int64_t new_timestamp;

	res = mkdir(path, mode);
	if (res == -1)
		return -errno;
	
	uint64_t para[10] = {0};
	para[0] = (int)mode;
	// add to sync queue
	new_timestamp = INCREASE_RECFS_CNT;
	recfs_update_timestamp(db, path, new_timestamp);
	sync_enqueue(MKDIR, (char*)path, "", para, NULL, NULL, new_timestamp, 0);
	return 0;
}

/*
 * No one can further change deleted file (i.e. the file in temp directory),
 * thus, we do not need to give those files timestamp. But since we have to 
 * check the validation of relations, we choose to give timestamp to those files.
 */
static int xmp_unlink(const char *path)
{
	int res;
	char tmp_path[PATH_LEN];
	
	uint64_t new_timestamp;
	uint64_t file_version;
	recfs_get_timestamp(db, path, &file_version);
	
	sync_pack_write_path(path);

	// remove relation with the dst of path, comment added by zql
	// can be removed since rel_find can check whether a relation is available
	rel_delete_dst((char*)path);
	rel_delete_src((char*)path);

	rename_tmp((char*)path, tmp_path);
	res = rename((char*)path, tmp_path);
	if ( res == -1)
	{
		return -errno;
	}
	else
	{
		new_timestamp = INCREASE_RECFS_CNT;
		//new_timestamp = file_version;
		sync_enqueue(RENAME, (char*)path, tmp_path, NULL, NULL, NULL, new_timestamp, file_version);

		recfs_update_timestamp(db, tmp_path, new_timestamp);
		recfs_del_timestamp(db, path); //TODO:bugfix
		rel_enqueue((char*)path, tmp_path, new_timestamp);
		return 0;
	}

	// TODO: we do not check res of unlink, so we should do rename according to 
	// the result of unlink in future
	/*res = unlink(path);
	if (res == -1)
		return -errno;
	return 0;*/
}

static int xmp_rmdir(const char *path)
{
	int res;
	uint64_t dir_version;
	recfs_get_timestamp(db, path, &dir_version);

	sync_enqueue(RMDIR, (char*)path, "", NULL, NULL, NULL, dir_version, 0);
	
	res = rmdir(path);
	if (res == -1)
		return -errno;
	return 0;
}

/*
 * Do not support sync of symlink in this version.
 */
static int xmp_symlink(const char *from, const char *to)
{
	int res;

	res = symlink(from, to);
	if (res == -1)
		return -errno;
	return 0;
}

static int xmp_rename(const char *from, const char *to)
{
	int res;

	uint64_t new_timestamp;
	uint64_t from_version;
	recfs_get_timestamp(db, from, &from_version);
	recfs_del_timestamp(db, from); //TODO:bugfix
	new_timestamp = INCREASE_RECFS_CNT;

	// check whether the destination file exists
	// TODO: this open is used to check whether "to" exists,
	// the corresponding close() is shown below.
	int fd = open(to, O_RDONLY);
	rel_hash *rh = rel_find((char*)to);

	/*
	 * if dst file not exists and not has a relation
	 */
	if (fd == -1 && rh == NULL)
	{
		sync_pack_write_path(from);
		recfs_update_timestamp(db, to, new_timestamp);
		sync_enqueue(RENAME, (char*)from, (char*)to, NULL, NULL, NULL, new_timestamp, from_version);
	}
	/*
	 * dst exists. 
	 * TODO: if rh != NULL and fd != -1, this branch is also executed
	 */
	else if(fd != -1)
	{	
		// TODO: the "to" file should be locked here, check fuse for this.
		sync_pack_write_path(to);
		
		char dst[PATH_LEN] = {'\0'};
		strcpy(dst, (char*)to);
		
		// get the file's (char *to) timestamp.
		uint64_t file_timestamp;
		res = recfs_get_timestamp(db, to, &file_timestamp);
		assert(res == 0);

		char file_delta_path[PATH_LEN];
		int res = compute_delta((char*)to, (char*)from, (char*)to, (char*)to, file_delta_path);
		assert(res == 0); // 0 return right

		// check the file's (char *to) timestamp.
		uint64_t file_timestamp_end;
		res = recfs_get_timestamp(db, to, &file_timestamp_end);
		assert(file_timestamp == file_timestamp_end);

		recfs_update_timestamp(db, to, new_timestamp);
		
		// TODO: this unlink does not remove write buffer of "from" in sync queue
		sync_enqueue(UNLINK, (char*)from, "", NULL, NULL, NULL, from_version, 0);
		sync_delete(from);
		/* 
		 * new_timestamp is to's newest version;
		 * file_timestamp is to's (ie, based file) former version;
		 * here we use para to pass generated file's former version, ie, to's former version 
		 */
		// TODO: since para is long and timestamp is uint64_t, 
		// the program is correct when running on 64bit machine.
		sync_enqueue(DELTA, file_delta_path, dst, &file_timestamp, NULL, NULL, new_timestamp, file_timestamp);
	}
	/*
	 * relation exists
	 */
	else if(rh != NULL)
	{
		rel_node *rn = &rq->node[rh->loc];

		char dst[PATH_LEN] = {'\0'};
		strcpy(dst, (char*)to);
		
		char file_delta_path[PATH_LEN];
		int res = compute_delta(rn->dst, (char*)from, rn->dst, (char*)to, file_delta_path);
		assert(res == 0);

		/*
		 * since the delta is triggered by relation,
		 * we do not have to check the file's timestamp before and after 
		 * compute_delta. We only check whether the timestamp equals 
		 * the timestamp attached to the relation.
		 */
		uint64_t file_timestamp_check;
		recfs_get_timestamp(db, rn->dst, &file_timestamp_check);

		recfs_update_timestamp(db, to, new_timestamp);
		if (file_timestamp_check == rn->time) {
			sync_enqueue(UNLINK, (char*)from, "", NULL, NULL, NULL, from_version, 0);
			sync_delete(from);
			/* new_timestamp is to's newest version;
			 * file_timestamp_check is rn->dst's (ie based file) version;
			 * here we still use para to pass to's former version, but there is no to's former version in this case, so just pass 0 */
			uint64_t to_former_version = 0;
			sync_enqueue(DELTA, file_delta_path, dst, &to_former_version, NULL, NULL, new_timestamp, file_timestamp_check);
		}
		else {
			fprintf(stderr, "ERROR:delta failed as the relation(time) not match!\n");
			sync_pack_write_path(from);
			sync_enqueue(RENAME, (char*)from, (char*)to, NULL, NULL, NULL, new_timestamp, from_version);
		}
	}

	// close file
	if (fd != -1) close(fd);

	// delete relation
	//rel_delete(rh);
	//rel_delete_dst((char*)from);
	if (rh == NULL)
	{
	}
	else
	{
		rel_delete(rh);
	}

	/* TODO(by zql): this relation should not be added if it triggers delta */
	rel_enqueue((char*)from, (char*)to, new_timestamp);
	
	res = rename((char*)from, (char*)to);

	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_link(const char *from, const char *to)
{
	int res;

	uint64_t from_version;
	recfs_get_timestamp(db, from, &from_version);

	rel_hash *rh = rel_find((char*)to);
	/* trigger delta */
	if(rh)
	{
		rel_node *rn = &rq->node[rh->loc];
		sync_pack_write_path(rn->dst);
		sync_pack_write_path(from);
		
		char dst[PATH_LEN] = {'\0'};
		strcpy(dst, (char*)from);
		
		char file_delta_path[PATH_LEN];
		int res = compute_delta(rn->dst, (char*)from, rn->dst, (char*)from, file_delta_path);
		assert(res == 0);

		/* check the file's timestamp */
		uint64_t file_timestamp_check;
		recfs_get_timestamp(db, rn->dst, &file_timestamp_check);

		if (file_timestamp_check == rn->time) {
			/* 
			 * the last para is the version of the based file in delta.
			 * we do not record the former version of the generated file
			 * because if the based file is right the generated file,
			 * the former version is already recorded; however, if the 
			 * former version is renamed, there is no former version 
			 * on the server; if there is the former version, that means
			 * the conflict happens.
			 */
			// TODO: I set the middle timestamp (from's timestamp) to 0, which means currently we do not check from's timestamp on server.
			uint64_t delta_mid_timestamp = 0;
			sync_enqueue(DELTA, file_delta_path, dst, &delta_mid_timestamp, NULL, NULL, from_version, file_timestamp_check);
		
			// TODO: this sync_delete does nothing here, because "from" has been packed
			// we cannot find it in hash table.
			sync_delete(from);
		}
	}
	
	uint64_t new_timestamp;
	new_timestamp = INCREASE_RECFS_CNT;
	recfs_update_timestamp(db, to, new_timestamp);
	
	/* the first timestamp is for to, while the second one is for from */
	sync_enqueue(LINK, (char*)from, (char*)to, NULL, NULL, NULL, new_timestamp, from_version);

	res = link(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_chmod(const char *path, mode_t mode)
{
	int res;
	uint64_t new_timestamp = 0;
	uint64_t base_version = 0;

	// Since there is no change to file content,
	// we do not update file's version.
	//recfs_get_timestamp(db, path, &base_version);
	//new_timestamp = INCREASE_RECFS_CNT;
	//recfs_update_timestamp(db, path, new_timestamp);

	uint64_t para[PARA_LEN] = {0};
	para[0] = (int)mode;
	// here may not need write pack
	sync_enqueue(CHMOD, path, "", para, NULL, NULL, new_timestamp, base_version);

	res = chmod(path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_chown(const char *path, uid_t uid, gid_t gid)
{
	int res;
	uint64_t new_timestamp = 0;
	uint64_t base_version = 0;

	// Since there is no change to file content,
	// we do not update file's version.
	//recfs_get_timestamp(db, path, &base_version);
	//new_timestamp = INCREASE_RECFS_CNT;
	//recfs_update_timestamp(db, path, new_timestamp);
	
	uint64_t para[PARA_LEN] = {0};
	para[0] = (int)uid;
	para[1] = (int)gid;

	// here may not need write pack
	sync_enqueue(CHOWN, (char*)path, "", para, NULL, NULL, new_timestamp, base_version);

	res = lchown(path, uid, gid);
	if (res == -1)
		return -errno;

	return 0;
}

// TODO: maybe we should batch truncate into write buffer
static int xmp_truncate(const char *path, off_t size)
{
	int res;

	uint64_t para[PARA_LEN] = {0};

	// delete write buffer
	if(size == 0)
	{
		sync_delete_write((char *)path);
	}
	// pack
	else
	{
		sync_hash *sh = sync_find((char*)path);

		// append a write node
		if(sh && /*&sq->node[sh->loc] != NULL &&*/ sq->node[sh->loc].isNewfile != 1) 
		{
			// generate buf info
			buf_info *this_bi = (buf_info*) malloc (sizeof(buf_info));
			this_bi->offset = FLAG_TRUNCATE;
			this_bi->size = size;
			this_bi->next = NULL; this_bi->buf = NULL; this_bi->newbuf = NULL;
			sq->node[sh->loc].bn->bi_tail->next = this_bi;
			sq->node[sh->loc].bn->bi_tail = this_bi;
		}
		// add a truncate node
		else if (!sh)
		{
			uint64_t new_timestamp;
			uint64_t base_version;

			recfs_get_timestamp(db, path, &base_version);
			new_timestamp = INCREASE_RECFS_CNT;
			recfs_update_timestamp(db, path, new_timestamp);

			//printf("add a new sync node\n");
			para[0] = (int)size;
			sync_enqueue(TRUNCATE, (char*)path, "", para, NULL, NULL, new_timestamp, base_version);
		}
	}
	
    // deal with checksum
    /*struct stat filestat;
    stat(path, &filestat);
    if (filestat.st_size<size) { //enlarge
        checksum_enlarge(path,filestat.st_size,size);
    }
    if (filestat.st_size>size) { //shrink
        checksum_shrink(path,size);
    }*/

	res = truncate(path, size);
	if (res == -1)
		return -errno;
	return 0;
}

static int xmp_utimens(const char *path, const struct timespec ts[2])
{
	int res;
	struct timeval tv[2];
	uint64_t new_timestamp = 0;
	uint64_t base_version = 0;

	// for the same reason, we do not update file's version here.
	// get based version.
	//recfs_get_timestamp(db, path, &base_version);
	//new_timestamp = INCREASE_RECFS_CNT;
	//recfs_update_timestamp(db, path, new_timestamp);

	tv[0].tv_sec = ts[0].tv_sec;
	tv[0].tv_usec = ts[0].tv_nsec / 1000;
	tv[1].tv_sec = ts[1].tv_sec;
	tv[1].tv_usec = ts[1].tv_nsec / 1000;
	
	uint64_t para[10] = {0};
	para[0] = tv[0].tv_sec;
	para[1] = tv[0].tv_usec;
	para[2] = tv[1].tv_sec;
	para[3] = tv[1].tv_usec;

	// here may not need write pack
	sync_enqueue(UTIMENS, (char*)path, "", para, NULL, NULL, new_timestamp, base_version);

	res = utimes(path, tv);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_open(const char *path, struct fuse_file_info *fi)
{
	int res;

	res = open(path, fi->flags);
	if (res == -1)
		return -errno;

	close(res);
	return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
	int fd;
	int res;

	(void) fi;
	fd = open(path, O_RDONLY);
	if (fd == -1)
	return -errno;

	res = pread(fd, buf, size, offset);
	if (res == -1)
		res = -errno;
        else if (res != 0) {
            /*if (checksum_check(fd, path, buf, res, offset)) {
                fprintf(stderr,"read %s offset %d size %d res %d checksum error!\n",path,(int)offset,(int)size,(int)res);
                // TODO: whether res should be set as -1?
                res = -1;
            }*/
        }
	close(fd);
	return res;
}

static int xmp_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{
	int fd;
	int res;
	(void) fi;
	//decide whether to compute delta or sync write; whether copy on write
	int isAppend; 

	fd = open(path, O_RDWR);

    struct stat filestat;
    fstat(fd, &filestat);
    /*if (filestat.st_size<offset) { //enlarge
        checksum_enlarge(path,filestat.st_size,offset);
    }*/

	sync_hash *sh = sync_find((char*)path);

	if(!sh || (sh && /*&sq->node[sh->loc] != NULL &&*/ sq->node[sh->loc].isNewfile != 1))
	{
		long file_size = get_file_size((char*)path);
		if(offset >= file_size)
			isAppend = 1;
		else
			isAppend = 0;
		// generate buf info
		buf_info *this_bi = (buf_info*) malloc (sizeof(buf_info));
		this_bi->offset = offset;
		this_bi->size = size;
		this_bi->newbuf = (char *) malloc (sizeof(char)*size);
		this_bi->isAppend = isAppend;	
		this_bi->buf = (char *) malloc (sizeof(char)*size);
		
		pread(fd, this_bi->buf, size, offset);
		memcpy(this_bi->newbuf, buf, size);
		
		this_bi->next = NULL;
	
		if(sh) // find a sync node
		{
			//printf("find a sync node\n");
			sq->node[sh->loc].bn->bi_tail->next = this_bi;
			sq->node[sh->loc].bn->bi_tail = this_bi;
			//printf("append success\n");
		}
		else // add a new sync node
		{
			uint64_t new_timestamp;
			// read the (based) version of this file
			uint64_t base_version;
			recfs_get_timestamp(db, path, &base_version);

			// we only increase timestamp for the first write
			new_timestamp = INCREASE_RECFS_CNT;
			recfs_update_timestamp(db, path, new_timestamp);
			
			//printf("add a new sync node\n");
			//printf("%s is a in-placed write\n", (char*)path);
			// generate buf node
			buf_node *bn = (buf_node *) malloc (sizeof(buf_node));
			strcpy(bn->path, (char*)path); // TODO buf node path is useless
			bn->bi = this_bi;
			bn->bi_tail = this_bi;
			//bn->time = new_timestamp;
			bn->isNewfile = 0; // TODO isNewfile is useless
			bn->isDelete = 0;
			bn->isEmpty = 0; // TODO isEmpty is useless
			
			// WRITE is a standing flag, it will become WRITE or DELTA when packing
			sync_enqueue(WRITE, (char*)path, "", NULL, bn, NULL, new_timestamp, base_version); 
			//printf("add success\n");
		}
	}
	
	res = pwrite(fd, buf, size, offset);
	if (res == -1)
		res = -errno;
	/*else
		checksum_update(fd,path,buf,size,offset);*/

	close(fd);
    
	return res;
}

static int xmp_statfs(const char *path, struct statvfs *stbuf)
{
	int res;

	res = statvfs(path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_release(const char *path, struct fuse_file_info *fi)
{
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */
	// can not do it because temporary file should not upload
	// if(buf_find((char*)path)) execute_this_path((char*)path);
	// delete the newfile hash
	
	/*
	newfile_hash *nh = newfile_find((char*)path);
	if(nh) HASH_DEL(nhs, nh);
	sync_hash *sh = sync_find(path);
	if(sh) 
	{
		// why not pack directly? 
		sync_pack_write(&sq->node[sh->loc]);
		HASH_DEL(shs, sh);
	}
	*/
	(void) path;
	(void) fi;
	return 0;
}

static int xmp_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi)
{
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */
	int res;

	res = open(path, fi->flags);
	if (res == -1)
		return -errno;

    if (isdatasync)
        fdatasync(res);
    else
        fsync(res);

	close(res);
    return 0;
}

static int xmp_flush(const char *path, struct fuse_file_info *fi)
{
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */
    return 0;
}
#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int xmp_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags)
{
	int res = lsetxattr(path, name, value, size, flags);
	if (res == -1)
		return -errno;
	return 0;
}

static int xmp_getxattr(const char *path, const char *name, char *value,
			size_t size)
{
	int res = lgetxattr(path, name, value, size);
	if (res == -1)
		return -errno;
	return res;
}

static int xmp_listxattr(const char *path, char *list, size_t size)
{
	int res = llistxattr(path, list, size);
	if (res == -1)
		return -errno;
	return res;
}

static int xmp_removexattr(const char *path, const char *name)
{
	int res = lremovexattr(path, name);
	if (res == -1)
		return -errno;
	return 0;
}
#endif /* HAVE_SETXATTR */

static struct fuse_operations xmp_oper = {
	.getattr	= xmp_getattr,
	.access		= xmp_access,
	.readlink	= xmp_readlink,
	.readdir	= xmp_readdir,
	.mknod		= xmp_mknod,
	.mkdir		= xmp_mkdir,
	.symlink	= xmp_symlink,
	.unlink		= xmp_unlink,
	.rmdir		= xmp_rmdir,
	.rename		= xmp_rename,
	.link		= xmp_link,
	.chmod		= xmp_chmod,
	.chown		= xmp_chown,
	.truncate	= xmp_truncate,
	.utimens	= xmp_utimens,
	.open		= xmp_open,
	.read		= xmp_read,
	.write		= xmp_write,
	.statfs		= xmp_statfs,
	.release	= xmp_release,
	.fsync		= xmp_fsync,
    .flush      = xmp_flush,
#ifdef HAVE_SETXATTR
	.setxattr	= xmp_setxattr,
	.getxattr	= xmp_getxattr,
	.listxattr	= xmp_listxattr,
	.removexattr	= xmp_removexattr,
#endif
};

void *worker1(char *argv[])
{
    fuse_main(FUSE_ARGC, argv, &xmp_oper, NULL);
	return NULL;
}

void *worker2(void *data)
{
	rel_execute();
	return NULL;
}

void *worker4(void *data)
{
	sync_execute();
	return NULL;
}

void *worker5(void *data)
{
	checksum_checksync();
	return NULL;
}

/*void *worker6(void *data)
{
	receive_data_from_server();
	return NULL;
}*/

int main(int argc, char *argv[])
{
    pthread_t th1, th2, th4,th5, th6;
	umask(0);

    initial();

    pthread_create(&th1, NULL, (void *)worker1, argv);
    pthread_create(&th2, NULL, (void *)worker2, NULL);
    pthread_create(&th4, NULL, (void *)worker4, NULL);
    pthread_create(&th5, NULL, (void *)worker5, NULL);
    //pthread_create(&th6, NULL, (void *)worker6, NULL);
   
    pthread_join(th1, NULL);
    pthread_join(th2, NULL); 
	pthread_join(th4, NULL);
	pthread_join(th5, NULL);
	//pthread_join(th6, NULL);
        
    return 0;
}
