#include "syncchecksum.h"
#include <assert.h>

#define CHECKSUM_BLOCK_LEN RS_DEFAULT_BLOCK_LEN
#ifndef CHECKSUM_DB_BLOCKNUM
#define CHECKSUM_DB_BLOCKNUM 4
#endif //CHECKSUM_DB_BLOCKNUM
#define CHECKSUM_DB_BLOCKSIZE (CHECKSUM_DB_BLOCKNUM*sizeof(uint32_t))
#define CHECKSUM_ZEROBLOCK 0x7c00f800
#define CHECKSUM_ENABLED 1

extern leveldb_t *db;

void checksum_checksync() {
    #ifndef CHECKSUM_ENABLED
        return;
    #endif
    leveldb_writeoptions_t *woptions;
    woptions = leveldb_writeoptions_create();
    leveldb_writeoptions_set_sync(woptions,1);
    char *err= NULL;
    for (;;) {
        sleep(1);
        leveldb_put(db,woptions,"cloudsync_syncflag",18,"",0,&err);
        if (err != NULL) {
            fprintf(stderr,"checksum_checksync leveldb_put() error!\n");
        }
        leveldb_free(err); err = NULL;
    }
}

void checksum_update(int fd,const char *path, const char *buf, size_t size, off_t offset) {
    #ifndef CHECKSUM_ENABLED
        return;
    #endif
    char key[PATH_LEN+4];
    uint32_t pathlen = strlen(path);
    memcpy(key,path,pathlen);

    uint32_t cur_block;
    uint32_t cur_offset, remainder;//buf_offset
    unsigned int tmpsum;
    char tmpblk[CHECKSUM_BLOCK_LEN], remain_flag;
    size_t preadsize;

    char *err = NULL;
    leveldb_writeoptions_t *woptions;
    woptions = leveldb_writeoptions_create();

    // handle the case that offset and offset+size are in 
    // the same block separately
    if (offset >> 11 == (offset+size) >> 11) {
        cur_block = offset >> 11;
        cur_offset = (offset >> 11) << 11;
        memset(tmpblk, 0, CHECKSUM_BLOCK_LEN);
        preadsize = pread(fd, tmpblk, CHECKSUM_BLOCK_LEN, cur_offset);
        if (preadsize <= 0) {
            fprintf(stderr, "checksum_update pread error: %ld\n", preadsize);
        }
        memcpy(tmpblk + (offset&0x000007ff), buf, size);
        tmpsum = rs_calc_weak_sum(tmpblk, CHECKSUM_BLOCK_LEN);
        key[pathlen] = '\0';
        memcpy(key+pathlen, &cur_block, sizeof(uint32_t));
        leveldb_put(db, woptions, key, pathlen+4, (const char*)&tmpsum, sizeof(tmpsum), &err);
        if (err != NULL) {
            fprintf(stderr, "checksum_update leveldb_put() error!\n");
            leveldb_free(err);
            err = NULL;
        }
        return;
    }

    // handle the first block which is not aligned 
    cur_block = offset >> 11;
    if (offset % CHECKSUM_BLOCK_LEN != 0) {
        cur_offset = (offset >> 11) << 11;
        memset(tmpblk, 0, CHECKSUM_BLOCK_LEN);
        preadsize = pread(fd, tmpblk, CHECKSUM_BLOCK_LEN, cur_offset);
        if (preadsize <= 0) {
            fprintf(stderr, "checksum_update pread error: %ld\n", preadsize);
        }
        assert((offset - cur_offset) == (offset & 0x000007ff));
        remainder = CHECKSUM_BLOCK_LEN - (offset & 0x000007ff);
        memcpy(tmpblk + (offset & 0x000007ff), buf, remainder);
        tmpsum = rs_calc_weak_sum(tmpblk, CHECKSUM_BLOCK_LEN);
        memcpy(key+pathlen, &cur_block, sizeof(uint32_t));
        leveldb_put(db, woptions, key, pathlen+4, (const char*)&tmpsum, sizeof(tmpsum), &err);
        if (err != NULL) {
            fprintf(stderr, "checksum_update leveldb_put() error!\n");
            leveldb_free(err);
            err = NULL;
        }
        *((uint32_t*)(key+pathlen)) += 1;
        remain_flag = 1;
    }
    else {
        memcpy(key+pathlen, &cur_block, sizeof(uint32_t));
        remainder = 0;
        remain_flag = 0; // for alinged case
    }

    // handle middle blocks
    cur_offset = ((offset >> 11)+remain_flag) << 11;
    //while (cur_offset < ((offset+size)&0x000007ff)) {
    while (cur_offset < ((offset+size) >> 11) << 11) {
        tmpsum = rs_calc_weak_sum(buf+remainder, CHECKSUM_BLOCK_LEN);
        leveldb_put(db, woptions, key, pathlen+4, (const char*)&tmpsum, sizeof(tmpsum), &err);
        if (err != NULL) {
            fprintf(stderr, "checksum_update leveldb_put() error!\n");
            leveldb_free(err);
            err = NULL;
        }
        *((uint32_t*)(key+pathlen)) += 1;
        remainder += CHECKSUM_BLOCK_LEN;
        cur_offset += CHECKSUM_BLOCK_LEN;
    }

    // handle the last block which is not aligned 
    if ((offset + size) % CHECKSUM_BLOCK_LEN != 0) {
        assert(cur_offset == ((offset + size) >> 11) << 11);
        memset(tmpblk, 0, CHECKSUM_BLOCK_LEN);
        // TODO: to handle read error and end of file separately
        preadsize = pread(fd, tmpblk, CHECKSUM_BLOCK_LEN, cur_offset);
        memcpy(tmpblk, buf+remainder, (offset+size)&0x000007ff);
        tmpsum = rs_calc_weak_sum(tmpblk, CHECKSUM_BLOCK_LEN);
        leveldb_put(db, woptions, key, pathlen+4, (const char*)&tmpsum, sizeof(tmpsum), &err);
        if (err != NULL) {
            fprintf(stderr, "checksum_update leveldb_put() error!\n");
            leveldb_free(err);
            err = NULL;
        }
    }
    return;
}

void checksum_enlarge(const char *path, off_t oldsize, off_t newsize) {
    #ifndef CHECKSUM_ENABLED
        return;
    #endif
    uint32_t blockhead,blockrear,i;
    if (oldsize>0) blockhead=((oldsize-1)>>11)+1; else blockhead=0;
    blockrear=(newsize-1)>>11; //newsize > oldsize > 0
    char key[PATH_LEN+4];
    uint32_t pathlen = strlen(path);
    memcpy(key,path,pathlen);
    unsigned int zero=CHECKSUM_ZEROBLOCK;
    char *err = NULL;
    leveldb_writeoptions_t *woptions;
    woptions = leveldb_writeoptions_create();
    for (i=blockhead;i<=blockrear;++i) {
        memcpy(key+pathlen,&i,4);
        leveldb_put(db, woptions, key, pathlen+4, (const char*)&zero, sizeof(zero), &err);
        if (err != NULL) {
            fprintf(stderr, "checksum_shrink leveldb_put() error!\n");
            leveldb_free(err);
            err = NULL;
        }
    }
}

void checksum_shrink(const char *path, off_t newsize) {
    #ifndef CHECKSUM_ENABLED
        return;
    #endif
    if (newsize==0) return;
    unsigned int tmpsum;
    char tmpblk[CHECKSUM_BLOCK_LEN];
    memset(tmpblk,0,sizeof(tmpblk));
    off_t offset=((newsize-1)>>11)<<11;
    size_t readsize=newsize-offset;
    uint32_t blockid=offset>>11;

    char key[PATH_LEN+4];
    uint32_t pathlen = strlen(path);
    memcpy(key,path,pathlen);
    memcpy(key+pathlen,&blockid,4);

    int fd = open(path, O_RDONLY);
    int preadsize = pread(fd, tmpblk, readsize, offset);
    assert(preadsize == readsize);
    close(fd);
    tmpsum = rs_calc_weak_sum(tmpblk, CHECKSUM_BLOCK_LEN);

    char *err = NULL;
    leveldb_writeoptions_t *woptions;
    woptions = leveldb_writeoptions_create();

    leveldb_put(db, woptions, key, pathlen+4, (const char*)&tmpsum, sizeof(tmpsum), &err);
        if (err != NULL) {
        fprintf(stderr, "checksum_shrink leveldb_put() error!\n");
        leveldb_free(err);
        err = NULL;
    }
}

int checksum_check(int fd,const char *path, char *buf, size_t size, off_t offset){
    #ifndef CHECKSUM_ENABLED
        return 0;
    #endif
    char key[PATH_LEN+4];
    char zqltest[PATH_LEN];
    uint32_t pathlen = strlen(path);
    memcpy(key, path, pathlen);

    uint32_t cur_block;
    uint32_t cur_offset, remainder;
    unsigned int tmpsum;
    char tmpblk[CHECKSUM_BLOCK_LEN];

    char *read_chksum, *err = NULL;
    size_t readlen;
    leveldb_readoptions_t *roptions;
    roptions = leveldb_readoptions_create();

    // handle the case that offset and offset+size are in 
    // the same block separately
    if (offset >> 11 == (offset+size) >> 11) {
        cur_block = offset >> 11;
        cur_offset = ((offset) >> 11) << 11;
        memset(tmpblk, 0, CHECKSUM_BLOCK_LEN);
        pread(fd, tmpblk, CHECKSUM_BLOCK_LEN, cur_offset);
        tmpsum = rs_calc_weak_sum(tmpblk, CHECKSUM_BLOCK_LEN);
        memcpy(key+pathlen, &cur_block, sizeof(uint32_t));
        read_chksum = leveldb_get(db, roptions, key, pathlen+4, &readlen, &err);
        if (err != NULL) {
            fprintf(stderr, "checksum_check leveldb_get() error!\n");
            leveldb_free(err);
            err = NULL;
        }
        if (tmpsum != *(unsigned int*)read_chksum) {
            leveldb_free(read_chksum);
            return -1;
        }
        return 0;
    }

    // handle the first block which is not aligned 
    cur_block = offset >> 11;
    if (offset % CHECKSUM_BLOCK_LEN != 0) {
        cur_offset = (offset >> 11) << 11;
        memset(tmpblk, 0, CHECKSUM_BLOCK_LEN);
        pread(fd, tmpblk, CHECKSUM_BLOCK_LEN, cur_offset);
        tmpsum = rs_calc_weak_sum(tmpblk, CHECKSUM_BLOCK_LEN);
        memcpy(zqltest, key, pathlen);
        zqltest[pathlen] = '\0';
        memcpy(key+pathlen, &cur_block, sizeof(uint32_t));
        read_chksum = leveldb_get(db, roptions, key, pathlen+4, &readlen, &err);
        if (err != NULL) {
            fprintf(stderr, "checksum_check leveldb_get() error!\n");
            leveldb_free(err);
            err = NULL;
        }
        if (tmpsum != *(unsigned int*)read_chksum) {
            // TODO: how to free roptions?
            //leveldb_free(roptions);
            leveldb_free(read_chksum);
            return -1;
        }
        cur_offset += CHECKSUM_BLOCK_LEN;
        leveldb_free(read_chksum);
        remainder = CHECKSUM_BLOCK_LEN - (offset & 0x000007ff);
        *((uint32_t*)(key+pathlen)) += 1;
    }
    else {
        memcpy(key+pathlen, &cur_block, sizeof(uint32_t));
        cur_offset = offset;
        remainder = 0;
    }

    // handle middle blocks
    //cur_offset = offset;
    //remainder = CHECKSUM_BLOCK_LEN - (offset & 0x000007ff);
    //while (cur_offset < ((offset+size) & 0x000007ff)) {
    while (cur_offset < ((offset+size) >> 11) << 11) {
        tmpsum = rs_calc_weak_sum(buf+remainder, CHECKSUM_BLOCK_LEN);
        memcpy(zqltest, key, pathlen);
        zqltest[pathlen] = '\0';
        read_chksum = leveldb_get(db, roptions, key, pathlen+4, &readlen, &err);
        if (err != NULL) {
            fprintf(stderr, "checksum_check leveldb_get() error!\n");
            leveldb_free(err);
            err = NULL;
        }
        if (tmpsum != *(unsigned int*)read_chksum) {
            leveldb_free(read_chksum);
            printf("verify error: ---\n");
            return -1;
        }
        remainder += CHECKSUM_BLOCK_LEN;
        cur_offset += CHECKSUM_BLOCK_LEN;
        *((uint32_t*)(key+pathlen)) += 1;
        leveldb_free(read_chksum);
    }

    // handle the last block which is not aligned 
    if ((offset + size) % CHECKSUM_BLOCK_LEN != 0) {// &&
    //        offset >> 11 != (offset+size) >> 11) {
        assert(cur_offset == ((offset+size) >> 11) << 11);
        memset(tmpblk, 0, CHECKSUM_BLOCK_LEN);
        pread(fd, tmpblk, CHECKSUM_BLOCK_LEN, cur_offset);
        tmpsum = rs_calc_weak_sum(tmpblk, CHECKSUM_BLOCK_LEN);
        // *((uint32_t*)(key+pathlen)) += 1;
        memcpy(zqltest, key, pathlen);
        zqltest[pathlen] = '\0';
        read_chksum = leveldb_get(db, roptions, key, pathlen+4, &readlen, &err);
        if (err != NULL) {
            fprintf(stderr, "checksum_check leveldb_get() error!\n");
            leveldb_free(err);
            err = NULL;
        }
        if (tmpsum != *(unsigned int*)read_chksum) {
            leveldb_free(read_chksum);
            return -1;
        }
        leveldb_free(read_chksum);
    }
    return 0;
}

