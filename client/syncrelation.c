#include "syncrelation.h"
#include <unistd.h>

extern rel_queue *rq;
extern rel_hash *rhs;


// we use __sync_bool_compare_and_swap
void rel_enqueue(char *src, char *dst, time_t localtime)
{
	printf("\n<-- enqueue a rel node -->\n");
	printf("%s %s\n", src, dst);
	int p, q;
	do
	{
		// TODO: it is ugly since it still has ABA problem
		// we can use double-CAS to solve it according to the paper
rel_queue_retest:
		p = rq->tail;
		if (!__sync_bool_compare_and_swap(&rq->node[rq->tail].isEmpty, 1, 1))
		// check whether the queue is full
		{
			printf("Relation queue is full, enqueue failed.\n");
			//return;
			sleep(1);
			goto rel_queue_retest;
		}
		q = (p + 1) % QUEUE_SIZE;
	}while(!__sync_bool_compare_and_swap(&rq->tail, p, q));
	// check tail = p, if so tail = q

	// find old relation
	rel_hash *rh = rel_find(src);
	if(rh)
	{
		rel_delete(rh);
	}
	// new relation
	rh = (rel_hash*) malloc (sizeof(rel_hash));
	memset(rh, 0, sizeof(rel_hash)); /* zero fill! */
	strcpy(rh->src, src);
	rh->loc = p;
    pthread_rwlock_wrlock(&rq->hash_rwlock);
	HASH_ADD(hh, rhs, src, sizeof(char)*PATH_LEN, rh);
    pthread_rwlock_unlock(&rq->hash_rwlock);
		
	strcpy(rq->node[p].src, src); // flag should be set to 1 at last
	strcpy(rq->node[p].dst, dst);
	rq->node[p].time = localtime;
	rq->node[p].isDelete = 0;
	rq->node[p].isEmpty = 0;
}

void rel_dequeue()
{
	printf("\n<-- dequeue a rel node -->\n");

	int q = rq->head;

	if (rq->node[q].isEmpty == 1)
	{
		printf("Relation queue is empty, dequeue failed.\n");
		return;
	}
	printf("%s %s\n", rq->node[q].src, rq->node[q].dst);
	
	rel_hash *rh = rel_find(rq->node[q].src);
	if(rh && rq->node[q].isDelete == 0)
	{
		rel_delete(rh);
	}

	rq->head = (rq->head + 1) % QUEUE_SIZE;
	rq->node[q].isEmpty = 1;
}

rel_hash* rel_find(char *src)
{
    //printf("\n<-- find a rel node -->\n");
	rel_hash l, *p;
	//find hash
	memset(&l, 0, sizeof(rel_hash)); /* zero fill! */
	strcpy(l.src, src);
    //pthread_rwlock_rdlock(&rq->hash_rwlock);
	HASH_FIND(hh, rhs, &l.src, sizeof(char)*PATH_LEN, p);
    //pthread_rwlock_unlock(&rq->hash_rwlock);
	if(p)
	{
		//printf("\n<-- find rel -->\n");
		return p;
	}
	//printf("\n<-- not find rel -->\n");
    return NULL;
}

void rel_delete(rel_hash *r)
{
    printf("\n<-- delete a rel node -->\n");
	if(r)
	{
        pthread_mutex_lock(&rq->node[r->loc].mutex);
		rq->node[r->loc].isDelete = 1;
        pthread_mutex_unlock(&rq->node[r->loc].mutex);
		HASH_DEL(rhs, r);
        free(r);
	}
}

void rel_delete_src(char *src)
{
    pthread_rwlock_wrlock(&rq->hash_rwlock);
	rel_hash l, *p;
	//find hash
	memset(&l, 0, sizeof(rel_hash)); /* zero fill! */
	strcpy(l.src, src);
	HASH_FIND(hh, rhs, &l.src, sizeof(char)*PATH_LEN, p);
	if(p) {
        rel_delete(p);
	}
    pthread_rwlock_unlock(&rq->hash_rwlock);
}

void rel_delete_dst(char *dst)
{
    pthread_rwlock_wrlock(&rq->hash_rwlock);
	rel_hash *rh;
	for(rh = rhs; rh != NULL; rh = rh->hh.next)
	{
		if(strcmp(dst, rq->node[rh->loc].dst) == 0)
		{
			rel_delete(rh);
		}
	}
    pthread_rwlock_unlock(&rq->hash_rwlock);
}

void rel_print()
{
    printf("\n<-- print rel node -->\n");
	int iter = rq->head;
	rel_node *rn = &rq->node[iter];
	printf("rn->isEmpty = %d\n", rn->isEmpty);
    while(rn->isEmpty != 1)
    {
        printf("%s %s %d\n", rn->src, rn->dst, (int)rn->time);
        rn = &rq->node[++iter];
    }
}


