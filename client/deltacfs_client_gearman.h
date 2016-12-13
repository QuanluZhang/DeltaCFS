#ifndef _MYFUSEST_H_
#define _MYFUSEST_H_


#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <uthash.h>
#include <pthread.h>
#define PATH_LEN PATH_MAX
#define OP_LEN 200
#define NAME_LEN PATH_MAX
#define LOC_LEN 1000
#define PARA_LEN 5
#define QUEUE_SIZE 65536
#define FUSE_ARGC 5
#define MKNOD_MODE 33204
#define MKNOD_DEV 0

//#define MYPORT  8887
#define MYPORT  9090
#define DUMMY 0
#define FLAG_LEN 10
#define TMP_DIR_LEN 17
#define BUF_LEN 1024*256
#define FILE_LEN 1024*256
#define REL_BATCHTIME 3
#define SYNC_BATCHTIME 1
#define DELAY_WRITE_TIME 5 
#define WRITE_RATIO 0.5
#define INF 0x7FFFFFFF
#define FLAG_TRUNCATE 0xFFFFFFFF

// definination of operations
#define MKNOD 0
#define MKDIR 1
#define SYMLINK 2
#define RMDIR 3
#define LINK 4
#define CHMOD 5
#define CHOWN 6
#define TRUNCATE 7
#define UTIMENS 8
#define RENAME 9
#define UNLINK 10
#define NEWFILE 11
#define WRITE 12
#define DELTA 13

#define GEARMANIP "172.31.35.100"
#define GEARMANPORT 4730

//#define CLOSECONN 20 // represent the end of this socket connect

/*
 * define a macro for recfs_cnt += 1
 */
#define CLIENTID 0x00000001ffffffff
#define INCREASE_RECFS_CNT (__sync_add_and_fetch(&recfs_cnt, 1) & CLIENTID)

/*
 * This is a global parameter for incremental timestamp
 */
uint64_t recfs_cnt;

/* My opertions */
// record the replication relation
typedef struct rel_node
{
    char src[PATH_LEN];  /*zql: PATH_MAX is too big*/
    char dst[PATH_LEN];
	int isDelete; //1 is delete
	int isEmpty; //1 is empty
    pthread_mutex_t mutex; //lock
	uint64_t time; //TODO:system time_t is not accurate enough
}rel_node;

typedef struct rel_queue
{
    int head;
	int tail;
	rel_node node[QUEUE_SIZE]; /*zql: here can be constant size*/
    pthread_rwlock_t hash_rwlock; //relhash_lock
}rel_queue;                    /*zql: but the size needs to be thought over*/

// record old file's data when writing it

typedef struct buf_info
{
	off_t offset;
	size_t size;
	char *buf;
	char *newbuf;
	int isAppend; /*zql: can be removed*/
	struct buf_info *next;
}buf_info;

typedef struct buf_node
{
    char path[PATH_LEN];//path
	buf_info *bi;   /*zql: this can be constant array*/
	buf_info *bi_tail;  /*zql: if exceeds the constant size, */
	//uint64_t time;       /*zql: compact them and insert to another array*/
	int isNewfile;
	int isDelete;
	int isEmpty;
}buf_node;

// record sync operation
/*
	<newfile, path, "", NULL, file_buf>
	<write, path, "", bn, NULL>
	<delta, file_delta_path, "filename resname", NULL, NULL>
*/
/*TODO: use union to define this structure, the type of para is long
        but we have to make sure long can hold every parameter types */
/* TODO: op can be char, isPacked&isNewfile&isDelete&isEmpty can be in one char */
typedef struct sync_node
{
    int op;
    char src[PATH_LEN];
	char dst[PATH_LEN];
	//long para[PARA_LEN];
	uint64_t para[PARA_LEN];
	buf_node *bn;
	char *file_buf;
	int isPacked;
	int isNewfile;
	int isDelete;
	int isEmpty;
    pthread_mutex_t mutex; //lock
	uint64_t time; /* TODO: it records local timestamp, can be uint32_t */
	uint64_t baseVer; /* the timestamp of based version */
}sync_node;

typedef struct sync_hash
{
	char path[PATH_LEN];
	int loc;
	UT_hash_handle hh;
}sync_hash;

typedef struct sync_queue 
{
	int head;
	int tail;
    sync_node node[QUEUE_SIZE];  /*zqlzql: use array here, but have to check full and empty*/
    pthread_rwlock_t hash_rwlock; //synchash_lock
	/* cache TODO: verify */
    char hash_cache_path[PATH_LEN];
    sync_hash* hash_cache_result;
}sync_queue;

// hash the relation src and its location in queue
typedef struct rel_hash
{
	char src[PATH_LEN];
	int loc;
	UT_hash_handle hh;
}rel_hash;

/*
// record new file
typedef struct newfile_hash
{
	char path[PATH_LEN];
	int isNewfile;
	UT_hash_handle hh;
}newfile_hash;
*/
void initial();

void rel_execute();

void sync_execute();

/*static int xmp_getattr(const char *path, struct stat *stbuf);
static int xmp_access(const char *path, int mask);
static int xmp_readlink(const char *path, char *buf, size_t size);
static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi);
static int xmp_mknod(const char *path, mode_t mode, dev_t rdev);
static int xmp_mkdir(const char *path, mode_t mode);
static int xmp_unlink(const char *path);
static int xmp_rmdir(const char *path);
static int xmp_symlink(const char *from, const char *to);
static int xmp_rename(const char *from, const char *to);
static int xmp_link(const char *from, const char *to);
static int xmp_chmod(const char *path, mode_t mode);
static int xmp_chown(const char *path, uid_t uid, gid_t gid);
static int xmp_truncate(const char *path, off_t size);
static int xmp_utimens(const char *path, const struct timespec ts[2]);
static int xmp_open(const char *path, struct fuse_file_info *fi);
static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi);
static int xmp_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi);
static int xmp_statfs(const char *path, struct statvfs *stbuf);
static int xmp_release(const char *path, struct fuse_file_info *fi);
static int xmp_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi);
static int xmp_flush(const char *path, struct fuse_file_info *fi);
static int xmp_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags);
static int xmp_getxattr(const char *path, const char *name, char *value,
			size_t size);
static int xmp_listxattr(const char *path, char *list, size_t size);
static int xmp_removexattr(const char *path, const char *name);*/

void *worker1(char *argv[]);//fuse
void *worker2(void *data);	//rel_execute
void *worker4(void *data);	//sync_execute


#endif
