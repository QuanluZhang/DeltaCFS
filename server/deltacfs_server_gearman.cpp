/*
 * The DeltaCFS server.
 * Using gearman to provide a RPC-like API, which is easy to develop
 * and has low overhead in the testing environment.
 */

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cassert>
#include <sys/sendfile.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cinttypes>

#include <librsync.h>
#include <libgearman/gearman.h>
#include <leveldb/db.h>
#include <hiredis/hiredis.h>
// Utils recording timestamps, using redis
//#include "redisutil.h"
// Utils recording file versions, using leveldb
//#include "logutil.h"

// Compatible with old version of libgearman
#ifndef GEARMAN_FAIL
    #define GEARMAN_FAIL GEARMAN_FATAL
#endif
using namespace std;

// Some constants of array length
const int MAXNAMELEN=256;
const int VERSIONSTRLEN=17;//byte*8->hex*16

// Some parameters read in init()
char SERVERDIR[MAXNAMELEN];//="/tmp/inotifygearmanworker/";
char GEARMANIP[MAXNAMELEN];//="127.0.0.1";
int GEARMANPORT;//=4730;
int THREADNUM;//=1;
char LEVELDB_PATH[MAXNAMELEN];//="/tmp/cloudsyncserverdb";
char REDISIP[MAXNAMELEN];//="127.0.0.1";
int REDISPORT;//=6739;

leveldb::DB* leveldb_db;
redisContext *redisdb;

// File name stored in SERVERDIR is presented in hexadecimal format of file version
void ver_to_name(const char *version_hex,char *version_str) {
    sprintf(version_str,"%016" PRIx64,*(uint64_t*)version_hex);
}

/*
 * Client requesting a file from the server.
 * Not well tested, so removed in this version
 */
static void *func_get(gearman_job_st *job, void *cb_arg, size_t *result_size, gearman_return_t *ret_ptr) {
    return NULL;
}

/*
 * Truncate a file.
 * workload = old_version(8 bytes) + new_version(8 bytes) + filename(2 byte of length,then name with terminating '\0')
 *          + length (4 bytes)
 * File will be copied now, we may write a log or make a tag later.
 */
static void *func_truncate(gearman_job_st *job, void *cb_arg, size_t *result_size, gearman_return_t *ret_ptr) {

    const char *workload=(const char*)gearman_job_workload(job);
    size_t workload_size=gearman_job_workload_size(job);
    fprintf(stderr,"Received a truncate workload size %zu\n",workload_size);
    
    char oldver[VERSIONSTRLEN],newver[VERSIONSTRLEN];
    ver_to_name(workload,oldver);
    ver_to_name(workload+8,newver);
    int oldfd,newfd;
    oldfd=open(oldver,O_RDONLY);
    newfd=open(newver,O_WRONLY|O_CREAT|O_TRUNC,S_IRWXU);
    char name[MAXNAMELEN];
    uint16_t namelen;
    uint32_t truncatelen;
    memcpy(&namelen,workload+16,sizeof(namelen));
    memcpy(name,workload+18,namelen);
    memcpy(&truncatelen,workload+18+namelen,sizeof(truncatelen));

    struct stat filestat;
    fstat(oldfd,&filestat);
    //copy file(and truncate)
    ssize_t sendbytes=0,oldsize=filestat.st_size,res=0;
    if (oldsize>=truncatelen) {
        while (sendbytes<truncatelen) {
            res=sendfile(newfd,oldfd,NULL,truncatelen-sendbytes);
            assert(res>=0);
            sendbytes+=res;
        }
    } else {
        while (sendbytes<oldsize) {
            res=sendfile(newfd,oldfd,NULL,oldsize-sendbytes);
            assert(res>=0);
            sendbytes+=res;
        }
        ftruncate(newfd,truncatelen);
    }

    *ret_ptr=GEARMAN_SUCCESS;
    *result_size = 0;
    close(oldfd);
    close(newfd);

    return NULL;
}

/*
 * Upload a new file.
 * workload = new_version + filename + file_buffer (4 bytes of length, then buffer)
 * DeltaCFS will always call newfile instead of mknod (which create a null file)
 */
static void *func_newfile(gearman_job_st *job, void *cb_arg, size_t *result_size, gearman_return_t *ret_ptr) {

    const char *workload=(const char*)gearman_job_workload(job);
    size_t workload_size=gearman_job_workload_size(job);
    printf("Received a newfile workload size %zu\n",workload_size);
    
    char newver[VERSIONSTRLEN];
    ver_to_name(workload,newver);
    int newfd;
    char name[MAXNAMELEN];
    uint16_t namelen;
    uint32_t filelen;
    memcpy(&namelen,workload+8,sizeof(namelen));
    memcpy(name,workload+10,namelen);
    memcpy(&filelen,workload+10+namelen,sizeof(filelen));

    //write file
    newfd=open(newver,O_WRONLY|O_CREAT|O_TRUNC,S_IRWXU);
    ssize_t writebytes=0;
    int res=0;
    while (writebytes<filelen) {
        res=write(newfd,workload+14+namelen,filelen-writebytes);
        assert(res>=0);
        writebytes+=res;
    }

    *ret_ptr=GEARMAN_SUCCESS;
    *result_size = 0;
    close(newfd);

    return NULL;
}

/*
 * Write to a file
 * workload = old_version + new_version + filename(deprecated now)
 *          + write_opreations(each contains 4 bytes of offset, 4 bytes of size, and write buffer)
 * The old file will be moved to new name, and when needed,
 * we will restore the old file from log (which is not well tested and removed temporarily)
 */
static void *func_write(gearman_job_st *job, void *cb_arg, size_t *result_size, gearman_return_t *ret_ptr) {

    const char *workload=(const char*)gearman_job_workload(job);
    size_t workload_size=gearman_job_workload_size(job);
    printf("Received a write workload size %zu\n",workload_size);

    char oldver[VERSIONSTRLEN],newver[VERSIONSTRLEN];
    ver_to_name(workload,oldver);
    ver_to_name(workload+8,newver);

	// There will be always a old file, otherwise client will call newfile.
    bool ismknod=false;
	assert(*(uint64_t*)workload != 0);
	
    int newfd;
    uint16_t namelen;
    memcpy(&namelen,workload+16,sizeof(namelen));
    
    //deprecated but not removed yet
    char name[MAXNAMELEN];
    memcpy(name,workload+18,namelen);


    size_t workloadoffset=18+namelen;

    //deprecated but not removed yet
    if (ismknod) {
        newfd=open(newver,O_WRONLY|O_CREAT|O_TRUNC,S_IRWXU);
    } else {
        //move old file to new name
        rename(oldver,newver);
        newfd=open(newver,O_WRONLY|O_CREAT,S_IRWXU);
    }

    //do the writing job
    uint32_t offset,size;
    while (workloadoffset < workload_size) {
        offset=*(uint32_t*)(workload+workloadoffset);
        size=*(uint32_t*)(workload+workloadoffset+4);
        pwrite(newfd,workload+workloadoffset+8,size,offset);
        workloadoffset+=8+size;
    }

    *ret_ptr=GEARMAN_SUCCESS;
    *result_size = 0;
    close(newfd);

    return NULL;
}

/*
 * Use rsync delta to update a file
 * workload = sync_base_file(version) + old_version + new_version + filename + delta(same format of file buffer)
 */
//workload=basefile+oldversion+newversion+filename+delta
static void *func_delta(gearman_job_st *job, void *cb_arg, size_t *result_size, gearman_return_t *ret_ptr) {

    const char *workload=(const char*)gearman_job_workload(job);
    size_t workload_size=gearman_job_workload_size(job);
    printf("Received a delta workload size %zu\n",workload_size);
    
    char basefile[VERSIONSTRLEN],oldver[VERSIONSTRLEN],newver[VERSIONSTRLEN];
    ver_to_name(workload,basefile);
    ver_to_name(workload+8,oldver);
    ver_to_name(workload+16,newver);
    char name[MAXNAMELEN];
    uint16_t namelen;
    uint32_t deltalen;
    memcpy(&namelen,workload+24,sizeof(namelen));
    memcpy(name,workload+26,namelen);
    memcpy(&deltalen,workload+26+namelen,sizeof(deltalen));

    //write delta to tmpfile
    FILE *delta=tmpfile(),*newfile=fopen(newver,"wb");
    fwrite(workload+30+namelen,sizeof(char),deltalen,delta);
    //TODO:res of fwrite
    fseek(delta,0,SEEK_SET);

    //patch
    FILE *orig=fopen(basefile,"rb");
    if (orig==NULL) {
        *result_size = 0;
        fclose(delta);fclose(newfile);
		fprintf(stderr, "[CONFLICT]delta base file not found\n");
        return NULL;
    }
    rs_stats_t stats;
    rs_result rsyncresult;
    rsyncresult=rs_patch_file(orig, delta, newfile, &stats);
    if (rsyncresult != RS_DONE)
        *ret_ptr=GEARMAN_FAIL;
    else {
        *ret_ptr=GEARMAN_SUCCESS;
        printf("file patch success\n");
    }
    rs_log_stats(&stats);
    fclose(delta);fclose(newfile);fclose(orig);

    *result_size = 0;

    return NULL;
}


/*
 * Rename(move) a file.
 * workload = old_version + new_version + old_filename + new_filename
 * In fact, the two files can share the same version
 */
static void *func_rename(gearman_job_st *job, void *cb_arg, size_t *result_size, gearman_return_t *ret_ptr) {

    const char *workload=(const char*)gearman_job_workload(job);
    size_t workload_size=gearman_job_workload_size(job);
    printf("Received a rename workload size %zu\n",workload_size);

    char oldver[VERSIONSTRLEN], newver[VERSIONSTRLEN];
	uint16_t workloadoffset = 0;
	ver_to_name(workload, oldver);
	workloadoffset += sizeof(uint64_t);
	ver_to_name(workload+workloadoffset, newver);
	workloadoffset += sizeof(uint64_t);
    char fromname[MAXNAMELEN], toname[MAXNAMELEN];
    uint16_t namelen;
    memcpy(&namelen, workload+workloadoffset, sizeof(namelen));
	workloadoffset += sizeof(uint16_t);
    memcpy(fromname, workload+workloadoffset, namelen);
	workloadoffset += namelen;
    memcpy(&namelen, workload+workloadoffset, sizeof(namelen));
	workloadoffset += sizeof(uint16_t);
    memcpy(toname, workload+workloadoffset, namelen);
	workloadoffset += namelen;

	int res = rename(oldver, newver);
	if (res == -1) {
		fprintf(stderr, "server rename error: %d\n", errno);
		exit(-1);
	}

    *ret_ptr=GEARMAN_SUCCESS;
    *result_size = 0;

    return NULL;
}

/*
 * Rename(move) a file.
 * workload = version + filename
 * since we didn't introduce timestamp here, it does nothing
 */
static void *func_unlink(gearman_job_st *job, void *cb_arg, size_t *result_size, gearman_return_t *ret_ptr) {
    //read workload
    const char *workload=(const char*)gearman_job_workload(job);
    size_t workload_size=gearman_job_workload_size(job);
    printf("unlink working start workload size %zu\n",workload_size);

    //read data but do nothing
    char oldver[VERSIONSTRLEN];
	uint16_t workloadoffset = 0;
	ver_to_name(workload, oldver);
	workloadoffset += sizeof(uint64_t);     
    char name[MAXNAMELEN];
    uint16_t namelen;
    memcpy(&namelen, workload+workloadoffset, sizeof(namelen));
	workloadoffset += sizeof(uint16_t);
    memcpy(name, workload+workloadoffset, namelen);
	workloadoffset += namelen;

    //code about version and log will be updated here
    
    *ret_ptr=GEARMAN_SUCCESS;
    *result_size = 0;

    return NULL;
}

/*
 * Register gearman workers
 */
void *server_worker(void *data) {
    int id=(int)pthread_self();
    gearman_worker_st *worker=gearman_worker_create(NULL);
    gearman_return_t ret=gearman_worker_add_server(worker,GEARMANIP,GEARMANPORT);
    if (gearman_failed(ret)) {
        printf("thread %d worker creation failed! %d\n",id,ret);
        return NULL;
    }

    ret=gearman_worker_add_function(worker,"get",0,func_get,NULL);
    if (gearman_failed(ret)) {
        printf("thread %d worker add function get failed!\n",id);
        return NULL;
    }

    ret=gearman_worker_add_function(worker,"truncate",0,func_truncate,NULL);
    if (gearman_failed(ret)) {
        printf("thread %d worker add function truncate failed!\n",id);
        return NULL;
    }

    ret=gearman_worker_add_function(worker,"newfile",0,func_newfile,NULL);
    if (gearman_failed(ret)) {
        printf("thread %d worker add function newfile failed!\n",id);
        return NULL;
    }

    ret=gearman_worker_add_function(worker,"write",0,func_write,NULL);
    if (gearman_failed(ret)) {
        printf("thread %d worker add function write failed!\n",id);
        return NULL;
    }

    ret=gearman_worker_add_function(worker,"delta",0,func_delta,NULL);
    if (gearman_failed(ret)) {
        printf("thread %d worker add function delta failed!\n",id);
        return NULL;
    }

    ret=gearman_worker_add_function(worker,"rename",0,func_rename,NULL);
    if (gearman_failed(ret)) {
        printf("thread %d worker add function rename failed!\n",id);
        return NULL;
    }

    ret=gearman_worker_add_function(worker,"unlink",0,func_unlink,NULL);
    if (gearman_failed(ret)) {
        printf("thread %d worker add function unlink failed!\n",id);
        return NULL;
    }

    fprintf(stderr,"[INFO]thread %d worker start\n",id);
    for (;;) {
        ret=gearman_worker_work(worker);
        if (gearman_failed(ret))
            printf("The worker returns something wrong! %d\n",ret);
    }
    gearman_worker_free(worker);
    return NULL;
}

void init() {
    FILE *fp=fopen("server_setting.txt","r");
    fscanf(fp,"%s\n",SERVERDIR);
    fscanf(fp,"%s\n",GEARMANIP);
    fscanf(fp,"%d\n",&GEARMANPORT);
    fscanf(fp,"%d\n",&THREADNUM);
    fscanf(fp,"%s\n",LEVELDB_PATH);
    fscanf(fp,"%s\n",REDISIP);
    fscanf(fp,"%d\n",&REDISPORT);
    int res;
    res=chdir(SERVERDIR);
    if (res!=0) {
        fprintf(stderr,"chdir() error\n");
        assert(false);
    }

    fprintf(stderr,"[INFO]Config file readed successfully\n");
    
    leveldb::Options dboptions;
    dboptions.create_if_missing = true;
    leveldb::Status dbstatus=leveldb::DB::Open(dboptions,LEVELDB_PATH,&leveldb_db);
    assert(dbstatus.ok());
    fprintf(stderr,"[INFO]Leveldb opened successfully\n");

	struct timeval timeout = {1, 500000}; // 1.5 seconds
    redisdb=redisConnectWithTimeout(REDISIP, REDISPORT, timeout);
	if (redisdb == NULL || redisdb->err) {
		if (redisdb) {
			fprintf(stderr, "[ERROR]Redis connection error: %s\n", redisdb->errstr);
			redisFree(redisdb);
		} else {
			fprintf(stderr, "[ERROR]Redis connection error: can't allocate redis context\n");
		}
		exit(1);
	}

}

void cleanup() {
    delete leveldb_db;
    redisFree(redisdb);
}

int main() {

    init();

    pthread_t *threads=new pthread_t[THREADNUM];

    int i,res;

    for (i=0;i<THREADNUM;++i) {
        res=pthread_create(threads+i, NULL, server_worker, NULL);
        assert(res == 0);
    }

    for (int i=0;i<THREADNUM;++i) {
        res=pthread_join(threads[i],NULL);
        assert(res == 0);
    }

    //well actually cleanup() won't be called.
    cleanup();

    return 0;
}
