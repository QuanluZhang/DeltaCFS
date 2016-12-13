#include "syncqueue.h"
//#include "syncutil.h"
#include <unistd.h>
#include <assert.h>

extern void create_tmp(const char *path, char *tmp_path);
extern long get_file_size(const char *path);
extern char *dict(int op);
extern rel_hash* rel_find(char *src);
extern int compute_delta(char *originfile, char *newfile, char *basefile, char *resfile, char *file_delta_path);
//extern newfile_hash* newfile_find(char *path);
extern int recfs_get_timestamp(leveldb_t *db, const char *path, uint64_t *cnt);

extern sync_queue *sq;
extern sync_hash *shs;
extern rel_queue *rq;
//extern newfile_hash *nhs;

extern leveldb_t *db;

//只需给hash加锁，节点锁由isEmpty维护
//TODO: file_buf未用到
void sync_enqueue(int op, const char *src, char *dst, uint64_t *para, buf_node *bn, char *file_buf, time_t localtime, uint64_t base_version)
{
	printf("\n<-- enqueue a sync node -->\n");
	printf("%s %s %s\n", dict(op), src, dst);
	int p, q;
	do
	{
		// TODO: this is ugly since it still has ABA problem
		// we can double-CAS to solve it according to the paper
sync_queue_retest:
		p = sq->tail;
		if(!__sync_bool_compare_and_swap(&sq->node[sq->tail].isEmpty, 1, 1))
		// check whether the queue is full
		{
			printf("Sync queue is full, enqueue failed.\n");
			//return;
			sleep(1);
			goto sync_queue_retest;
		}
		q = (p + 1) % QUEUE_SIZE;
	}while(!__sync_bool_compare_and_swap(&sq->tail, p, q));
	
	// TODO multi threads compete: sync dequeue, rename, unlink
	// sync add hash
	if (op == NEWFILE || op == WRITE)
	{
		sync_hash *sh = (sync_hash*) malloc (sizeof(sync_hash));
		memset(sh, 0, sizeof(sync_hash)); /* zero fill! */
		strcpy(sh->path, src);
		sh->loc = p;
        pthread_rwlock_wrlock(&sq->hash_rwlock);
		HASH_ADD(hh, shs, path, sizeof(char)*PATH_LEN, sh);
        pthread_rwlock_unlock(&sq->hash_rwlock);
	}
	if (op == NEWFILE)
		sq->node[p].isNewfile = 1;
	else
		sq->node[p].isNewfile = 0;
		
	sq->node[p].op= op; // flag should be set to 1 at last
	strcpy(sq->node[p].src, src);
	strcpy(sq->node[p].dst, dst);
	int i;
	if(para != NULL)
	{
		for(i = 0; i < PARA_LEN; i++)
		{
			sq->node[p].para[i] = para[i];
		}
	}
	sq->node[p].bn = bn;
	if(file_buf != NULL)
	{
		sq->node[p].file_buf = file_buf;
	}
	else sq->node[p].file_buf = NULL;
	sq->node[p].time = localtime;
	if (op == NEWFILE || op == WRITE)
        sq->node[p].isPacked = 0;
    else
        sq->node[p].isPacked = 1;
	sq->node[p].isDelete = 0;
	sq->node[p].isEmpty = 0;
	sq->node[p].baseVer = base_version;
}

//仅由sync_execute调用，操作为释放队首元素；sync_execute为单线程故不会出队冲突；
//该node要么不是newfile write delta、要么一定会被pack/delete，【应当】不会node被访问，需要检查
//故函数内无需加锁
sync_node* sync_dequeue()
{
	printf("\n<-- dequeue a sync node -->\n");
	int q = sq->head;
	if (sq->node[q].isEmpty == 1)
	{
		printf("Sync queue is empty, dequeue failed.\n");
		return NULL;
	}
	printf("%s %s %s\n", dict(sq->node[q].op), sq->node[q].src, sq->node[q].dst);
	sq->head = (sq->head + 1) % QUEUE_SIZE;
	//if(sq->node[q].para != NULL) free(sq->node[q].para);
	if(sq->node[q].file_buf != NULL) free(sq->node[q].file_buf);
	sq->node[q].isPacked = 0;
	sq->node[q].isDelete = 1;
	sq->node[q].isEmpty = 1;

	buf_node *bn = sq->node[q].bn;
    buf_info *this_bi;
	if(bn)
	{
		while(bn->bi != NULL)
		{
            this_bi=bn->bi;
			if(bn->bi->buf != NULL)
				free(bn->bi->buf);
			if(bn->bi->newbuf != NULL)
				free(bn->bi->newbuf);
			bn->bi = bn->bi->next;
            free(this_bi);
		}
	}
	return &sq->node[q];
}

//调试用函数
void sync_print()
{
    printf("\n<-- print sync node -->\n");
	int iter = sq->head;
	sync_node *sn = &sq->node[iter];
	printf("sn->isEmpty = %d\n", sn->isEmpty);
    while(sn->isEmpty != 1)
    {
        printf("%s %s %s\n", dict(sn->op), sn->src, sn->dst);
        sn = &sq->node[++iter];
    }
}

// 在hash中找到【最近的】【与该文件相关的】【未被pack的】项，将其标记为deleted
// TODO: 加入时间戳之后查找部分需要注意
// TODO2: 是否只有rename会触发？其他的（比如pack、link）是否会触发？
// rename删除了buffer后插入了一个delta，pack直接将buffer替换成了delta；
void sync_delete(const char * path) /* zql: list condition of delete (here just rename)*/
{
    pthread_rwlock_wrlock(&sq->hash_rwlock);
	sync_hash *sh = sync_find(path);
    if(sh) {
        HASH_DEL(shs, sh);
        sq->hash_cache_result=NULL;
        sync_node *sn=&sq->node[sh->loc];
        free(sh);
        pthread_mutex_lock(&sn->mutex);
        sn->isDelete = 1;
        pthread_mutex_unlock(&sn->mutex);
        if (strcmp(path,sq->hash_cache_path)==0) sq->hash_cache_result=NULL;
    }
    pthread_rwlock_unlock(&sq->hash_rwlock);
}

//与sync_delete类似，但对NEWFILE不删除，只被truncate 0调用
void sync_delete_write(const char *path)
{
    pthread_rwlock_wrlock(&sq->hash_rwlock);
	sync_hash *sh = sync_find(path);
    if(sh) {
        HASH_DEL(shs, sh);
        sq->hash_cache_result=NULL;
        sync_node *sn=&sq->node[sh->loc];
        free(sh);
        pthread_mutex_lock(&sn->mutex);
        if (sn->op==NEWFILE) {
        	sn->op = MKNOD;
            sn->para[0] = MKNOD_MODE;
            sn->para[1] = MKNOD_DEV;
            sn->isPacked = 1;
        } else {
            sn->isDelete = 1;
        }
        pthread_mutex_unlock(&sn->mutex);
        if (strcmp(path,sq->hash_cache_path)==0) sq->hash_cache_result=NULL;
    }
    pthread_rwlock_unlock(&sq->hash_rwlock);
}

//封装了HASH_FIND
//目前内部不加锁，但是调用地点多于rel_find
sync_hash* sync_find(const char *path)
{
    if ((sq->hash_cache_result!=NULL)&&(strcmp(path,sq->hash_cache_path)==0)) return sq->hash_cache_result;
	sync_hash l, *p;
	//find hash
	memset(&l, 0, sizeof(sync_hash)); /* zero fill! */
	strcpy(l.path, path); //TODO: why copy the string?
	HASH_FIND(hh, shs, &l.path, sizeof(char)*PATH_LEN, p);
	if(p) {
        strcpy(sq->hash_cache_path,path);
        sq->hash_cache_result=p;
		return p;
	} else
		return NULL;
}

//内部维护锁，外部由sync_pack_write_name等维护
void sync_pack_write(sync_node *this_sn)
{
	// check whether it is a new file
	if(this_sn->op == NEWFILE)
	{
		printf("%s is a new file\n", this_sn->src);
		
		// TODO outside HASH_DELETE(sync_hash) should be deleted
		//pthread_rwlock_wrlock(&sq->hash_rwlock);
		//pthread_mutex_lock(&this_sn->mutex);
		//sync_hash *sh = sync_find(this_sn->src);
		//assert(sh != NULL);
		//if(sh) HASH_DEL(shs, sh);
		//pthread_rwlock_unlock(&sq->hash_rwlock);
        
		/* check whether the file has a relation */
		rel_hash *rh = rel_find(this_sn->src);
		/* compute delta */
		if(rh) 
		{
			rel_node *rn = &rq->node[rh->loc];
			// pack to
			//pthread_rwlock_rdlock(&sq->hash_rwlock);
			sync_hash *sh = sync_find(rn->dst);
			if(sh) 
			{//PROBLEM TODO:lock of synchash
				//shval=*sh;
				//pthread_rwlock_unlock(&sq->hash_rwlock);
				sync_pack_write(&sq->node[sh->loc]);
				//pthread_rwlock_wrlock(&sq->hash_rwlock);
				//sh = sync_find(rn->dst);
				HASH_DEL(shs, sh);
                sq->hash_cache_result=NULL;
                free(sh);
                //pthread_rwlock_unlock(&sq->hash_rwlock);
			}// else pthread_rwlock_unlock(&sq->hash_rwlock);

			char file_delta_path[PATH_LEN];
			int res = compute_delta(rn->dst, this_sn->src, rn->dst, this_sn->src, file_delta_path);
			assert(res == 0);

			/* check the file's (rn->dst) timestamp */
			uint64_t file_timestamp_check;
			recfs_get_timestamp(db, rn->dst, &file_timestamp_check);
			/*
			 * if true, upload delta file, changing info in sync_node
			 * if false, directly upload the file
			 */
			if (file_timestamp_check == rn->time) {
				/*char dst[PATH_LEN] = {'\0'};
				strcpy(dst, rn->dst);
				strcat(dst, " ");
				strcat(dst, this_sn->src);*/
				char dst[PATH_LEN] = {'\0'};
				strcpy(dst, this_sn->src);
			
				this_sn->op = DELTA;
				this_sn->baseVer = file_timestamp_check;
				// since it is newfile which does not exist on server
				this_sn->para[0] = 0;
				this_sn->isPacked = 1;
				
				strcpy(this_sn->src, file_delta_path);
				strcpy(this_sn->dst, dst);
				//pthread_mutex_unlock(&this_sn->mutex);
				
				return;
			}
			else {
				fprintf(stderr, "sync_pack_write delta timestamp changed\n");
			}
		}

		FILE *fp = fopen(this_sn->src, "rb+");
		
		long file_size;
		if(fp == NULL)
		{
			printf("open error\n");
			return;
		}
		file_size = get_file_size(this_sn->src);
		printf("file_size = %ld\n", file_size);
		
		this_sn->file_buf = (char *) malloc (sizeof(char) * (file_size));
		if (this_sn->file_buf == NULL)
			printf("Error: allocate memory\n");
		
		// send para to this_sn and execute it
		fread(this_sn->file_buf, file_size, 1, fp);
		this_sn->para[0] = file_size;
		// mknod info
		// this_sn->para[1] = MKNOD_MODE;
		// this_sn->para[2] = MKNOD_DEV;
		
		this_sn->isPacked = 1;
		//pthread_mutex_unlock(&this_sn->mutex);

		fclose(fp);
		return;
	}
	else if(this_sn->op == WRITE)
	{
		/*
		newfile_hash *nh = newfile_find(this_sn->src);
		assert(nh == NULL);
		*/
		// TODO outside HASH_DELETE(sync_hash) should be deleted
        //pthread_rwlock_wrlock(&sq->hash_rwlock);
        //pthread_mutex_lock(&this_sn->mutex);
		//sync_hash *sh = sync_find(this_sn->src);
		//assert(sh != NULL);
		//if(sh) HASH_DEL(shs, sh);
        //pthread_rwlock_unlock(&sq->hash_rwlock);

		printf("%s is not a new file\n", this_sn->src);
		// combine writing
		// compute writing size
		int write_size = 0;
		//int j = 0;
		int append_count = 0;
		int write_count = 0;
		float append_ratio = 0;
		long origin_file_size = INF;

		buf_info *this_bi = this_sn->bn->bi;
		while(this_bi != NULL)
		{
            if (this_bi->offset==FLAG_TRUNCATE) {
                this_bi = this_bi->next;
                continue;
            }
			write_size += this_bi->size;
			if(this_bi->isAppend == 1) 
			{
				append_count++;
				if(this_bi->offset < origin_file_size)
					origin_file_size = this_bi->offset;
			}
			write_count++;
			this_bi = this_bi->next;
		}
		if(write_count != 0)
			append_ratio = append_count*1.0/write_count;
		
		long file_size = get_file_size(this_sn->src);

		if(append_ratio > WRITE_RATIO || 1.0*write_size < WRITE_RATIO*file_size) // send write
		{
			printf("send append\n");
			// assign this sync node
			this_sn->op = WRITE;
			this_sn->isPacked = 1;
            //pthread_mutex_unlock(&this_sn->mutex);
		}
		else // send delta
		{
			printf("send delta\n");
			char tmp_path[PATH_LEN];
			create_tmp(this_sn->src, tmp_path);
			int fd = open(tmp_path, O_RDWR);
			this_bi = this_sn->bn->bi;
			while(this_bi != NULL)
			{
				if (this_bi->offset != FLAG_TRUNCATE) pwrite(fd, this_bi->buf, this_bi->size, this_bi->offset);
				this_bi = this_bi->next;
			}
			if(origin_file_size != INF)
				truncate(tmp_path, origin_file_size);
			close(fd);
			// compute delta and assign this_sn
			
			/*char dst[PATH_LEN] = {'\0'};
			strcpy(dst, this_sn->src);
			strcat(dst, " ");
			strcat(dst, this_sn->src);*/
			char dst[PATH_LEN] = {'\0'};
			strcpy(dst, this_sn->src);
		
			this_sn->op = DELTA;
			// since the file is generated based on it self
			this_sn->para[0] = this_sn->baseVer;
			this_sn->isPacked = 1;
			char file_delta_path[PATH_LEN];
			int res = compute_delta(tmp_path, this_sn->src, this_sn->src, this_sn->src, file_delta_path);
			assert(res == 0);
			strcpy(this_sn->src, file_delta_path);
			strcpy(this_sn->dst, dst);
            //pthread_mutex_unlock(&this_sn->mutex);
		
			// delete origin file
			remove(tmp_path);
		}
	}
	return;
}

void sync_pack_write_path(const char *path) {
    pthread_rwlock_wrlock(&sq->hash_rwlock);
    sync_hash *sh = sync_find((char*)path);
	if(sh)
	{
        pthread_mutex_lock(&sq->node[sh->loc].mutex);
		HASH_DEL(shs, sh);
        if (strcmp(path,sq->hash_cache_path)==0) sq->hash_cache_result=NULL;
		sync_pack_write(&sq->node[sh->loc]);
        pthread_rwlock_unlock(&sq->hash_rwlock);
        pthread_mutex_unlock(&sq->node[sh->loc].mutex);
        free(sh);
	} else
        pthread_rwlock_unlock(&sq->hash_rwlock);
}
