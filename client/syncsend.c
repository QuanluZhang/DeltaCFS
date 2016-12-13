#include "syncsend.h"

extern long get_file_size(const char *path);
extern gearman_client_st *gear_client;

/*
  Send a node of sync_queue up to cloud through socket
*/
//void sync_send(int socket_fd, sync_node *sn)
static char blob_buf[BLOBSIZE];

int sync_send(sync_node *sn)
{
	//char send_buf[BUF_LEN];
    //int num;
    //struct stat statbuff;
	//int32_t tmpint32;

	// send op
	//uint8_t tmpint8 = (uint8_t)sn->op;
	//send(socket_fd, &tmpint8, sizeof(uint8_t), 0);
	uint16_t tmpint16;
	uint32_t tmpint32;
	char *blob_bptr = blob_buf;
	size_t result_size;
	gearman_return_t rc;
	printf("helloooooooooooooooooooooooooooooooooooooooooooooooo\n");
	switch (sn->op) {
		/* the operations that change the inode */
		case RENAME:
		{
			// old version and new version
			memcpy(blob_bptr, &sn->baseVer, sizeof(uint64_t));
			blob_bptr += sizeof(uint64_t);
			memcpy(blob_bptr, &sn->time, sizeof(uint64_t));
			blob_bptr += sizeof(uint64_t);
			// source file name
			tmpint16 = strlen(sn->src) + 1;
			memcpy(blob_bptr, &tmpint16, sizeof(uint16_t));
			blob_bptr += sizeof(uint16_t);
			memcpy(blob_bptr, sn->src, tmpint16);
			blob_bptr += tmpint16;
			// destination file name
			tmpint16 = strlen(sn->dst) + 1;
			memcpy(blob_bptr, &tmpint16, sizeof(uint16_t));
			blob_bptr += sizeof(uint16_t);
			memcpy(blob_bptr, sn->dst, tmpint16);
			blob_bptr += tmpint16;
			assert(blob_bptr - blob_buf < BLOBSIZE);
			// send data using gearman
			void *value  = gearman_client_do(gear_client, "rename",
						NULL, blob_buf, blob_bptr-blob_buf,
						&result_size, &rc);
			if (!gearman_success(rc)) {
				fprintf(stderr, "Gearman rename failed!\n");
				//exit(0);
			}
			free(value);
			break;
		}
		case LINK:
			// do not support, do nothing
			break;
		case UNLINK:
		{
			// old version
			memcpy(blob_bptr, &sn->time, sizeof(uint64_t));
			blob_bptr += sizeof(uint64_t);
			// file name
			tmpint16 = strlen(sn->src) + 1;
			memcpy(blob_bptr, &tmpint16, sizeof(uint16_t));
			blob_bptr += sizeof(uint16_t);
			memcpy(blob_bptr, sn->src, tmpint16);
			blob_bptr += tmpint16;
			assert(blob_bptr - blob_buf < BLOBSIZE);
			// send data using gearman
			void *value  = gearman_client_do(gear_client, "unlink",
						NULL, blob_buf, blob_bptr-blob_buf,
						&result_size, &rc);
			if (!gearman_success(rc)) {
				fprintf(stderr, "Gearman unlink failed!\n");
				return -1;
			}
			free(value);
			break;
		}

		/* the operations that modify the file */
		case TRUNCATE:
		{
			// old version and new version
			memcpy(blob_bptr, &sn->baseVer, sizeof(uint64_t));
			blob_bptr += sizeof(uint64_t);
			memcpy(blob_bptr, &sn->time, sizeof(uint64_t));
			blob_bptr += sizeof(uint64_t);
			// file name
			tmpint16 = strlen(sn->src) + 1;
			memcpy(blob_bptr, &tmpint16, sizeof(uint16_t));
			blob_bptr += sizeof(uint16_t);
			memcpy(blob_bptr, sn->src, tmpint16);
			blob_bptr += tmpint16;
			// file length
			tmpint32 = (uint32_t)sn->para[0];
			memcpy(blob_bptr, &tmpint32, sizeof(uint32_t));
			blob_bptr += sizeof(uint32_t);
			assert(blob_bptr - blob_buf < BLOBSIZE);
			// send data using gearman
			void *value  = gearman_client_do(gear_client, "truncate",
						NULL, blob_buf, blob_bptr-blob_buf,
						&result_size, &rc);
			if (!gearman_success(rc)) {
				fprintf(stderr, "Gearman truncate failed!\n");
				return -1;
			}
			free(value);
			break;
		}
		case NEWFILE:
		{
			/*
			 * the headspace has been reserved for metadata in file_buf,
			 * and also has been filled with the metadata.
			 * we can just send it out.
			 * BUT at current stage, we directly copy data for easy debug
			 */
			// calculate the required space
			tmpint32 = 0;
			tmpint32 += sizeof(uint64_t);
			tmpint32 += sizeof(uint16_t);
			tmpint16 = strlen(sn->src) + 1;
			tmpint32 += tmpint16;
			tmpint32 += sizeof(uint32_t);
			tmpint32 += sn->para[0];
			// copy data
			uint32_t mem_size = tmpint32;
			char *write_buf = (char*)malloc(mem_size);
			char *write_bptr = write_buf;
			// new version
			memcpy(write_bptr, &sn->time, sizeof(uint64_t));
			write_bptr += sizeof(uint64_t);
			printf("new verison: %016lx\n", sn->time);
			// file name
			memcpy(write_bptr, &tmpint16, sizeof(uint16_t));
			write_bptr += sizeof(uint16_t);
			printf("filename: %04x\n", tmpint16);
			memcpy(write_bptr, sn->src, tmpint16);
			write_bptr += tmpint16;
			// file content
			uint32_t tmp_file_size = sn->para[0];
			memcpy(write_bptr, &tmp_file_size, sizeof(uint32_t));
			write_bptr += sizeof(uint32_t);
			printf("filesize: %08x\n", tmp_file_size);
			memcpy(write_bptr, sn->file_buf, sn->para[0]);
			write_bptr += sn->para[0];
			assert(mem_size == write_bptr - write_buf);
			// send data using gearman
			void *value  = gearman_client_do(gear_client, "newfile",
						NULL, write_buf, write_bptr-write_buf,
						&result_size, &rc);
			if (!gearman_success(rc)) {
				fprintf(stderr, "Gearman newfile failed!\n");
				return -1;
			}
			free(value);
			free(write_buf);
			break;
		}
		case WRITE:
		{
			// calculate the space size to allocate
			tmpint32 = 0;
			tmpint32 += (2*sizeof(uint64_t)); // old and new versions;
			tmpint32 += sizeof(uint16_t); // file name length
			tmpint16 = strlen(sn->src) + 1;
			tmpint32 += tmpint16; // file name length
			buf_info *this_bi = sn->bn->bi;
			while (this_bi != NULL) {
				// write operation
				if (this_bi->offset != FLAG_TRUNCATE) {
					tmpint32 += (this_bi->size + 2*sizeof(uint32_t));
				}
				// truncate operation
				else {
					tmpint32 += (2*sizeof(uint32_t));
				}
				this_bi = this_bi->next;
			}
			uint32_t mem_size = tmpint32;
			// copy data
			char *write_buf = (char *)malloc(mem_size);
			char *write_bptr = write_buf;
			memcpy(write_bptr, &sn->baseVer, sizeof(uint64_t));
			write_bptr += sizeof(uint64_t);
			memcpy(write_bptr, &sn->time, sizeof(uint64_t));
			write_bptr += sizeof(uint64_t);
			memcpy(write_bptr, &tmpint16, sizeof(uint16_t));
			write_bptr += sizeof(uint16_t);
			memcpy(write_bptr, sn->src, tmpint16);
			write_bptr += tmpint16;
			this_bi = sn->bn->bi;
			while (this_bi != NULL) {
				tmpint32 = (uint32_t)this_bi->offset;
				memcpy(write_bptr, &tmpint32, sizeof(uint32_t));
				write_bptr += sizeof(uint32_t);
				tmpint32 = (uint32_t)this_bi->size;
				memcpy(write_bptr, &tmpint32, sizeof(uint32_t));
				write_bptr += sizeof(uint32_t);
				if (this_bi->offset != FLAG_TRUNCATE) {
					memcpy(write_bptr, this_bi->newbuf, tmpint32);
					write_bptr += tmpint32;
				}
				this_bi = this_bi->next;
			}
			assert(mem_size == write_bptr - write_buf);
			// send data using gearman
			void *value  = gearman_client_do(gear_client, "write",
						NULL, write_buf, write_bptr-write_buf,
						&result_size, &rc);
			if (!gearman_success(rc)) {
				fprintf(stderr, "Gearman write failed!\n");
			}
			free(value);
			free(write_buf);
			break;
		}
		case DELTA:
		{
			/*
			 * the delta file is read here, so it is not the same
			 * with newfile operation, it is easier.
			 */
			// calculate required memory space
			tmpint32 = 0;
			tmpint32 += 3*sizeof(uint64_t);
			tmpint32 += sizeof(uint16_t);
			tmpint16 = strlen(sn->dst) + 1;
			tmpint32 += tmpint16;
			tmpint32 += sizeof(uint32_t);
			tmpint32 += get_file_size(sn->src);
			// copy data
			uint32_t mem_size = tmpint32;
			char *write_buf = (char *)malloc(mem_size);
			char *write_bptr = write_buf;
			// base version, target's old version and new version
			memcpy(write_bptr, &sn->baseVer, sizeof(uint64_t));
			write_bptr += sizeof(uint64_t);
			// NOTE: sn->para[0] stores target's old version
			//uint64_t tmptmp = 0;
			//memcpy(write_bptr, &(tmptmp), sizeof(uint64_t));
			memcpy(write_bptr, &(sn->para[0]), sizeof(uint64_t));
			write_bptr += sizeof(uint64_t);
			memcpy(write_bptr, &sn->time, sizeof(uint64_t));
			write_bptr += sizeof(uint64_t);
			// target's file name
			memcpy(write_bptr, &tmpint16, sizeof(uint16_t));
			write_bptr += sizeof(uint16_t);
			memcpy(write_bptr, sn->dst, tmpint16);
			write_bptr += tmpint16;
			// delta content
			uint32_t tmp_file_size = get_file_size(sn->src);
			memcpy(write_bptr, &tmp_file_size, sizeof(uint32_t));
			write_bptr += sizeof(uint32_t);
			int num = 0;
			int fd = open(sn->src, O_RDONLY);
			while ((num = read(fd, write_bptr, tmpint32)) > 0) {
				write_bptr += num;
			}
			assert(write_bptr - write_buf == mem_size);
			close(fd);
			// send data using gearman
			void *value  = gearman_client_do(gear_client, "delta",
						NULL, write_buf, write_bptr-write_buf,
						&result_size, &rc);
			if (!gearman_success(rc)) {
				fprintf(stderr, "Gearman delta failed!\n");
				return -1;
			}
			free(value);
			free(write_buf);
			// remove delta file
			remove(sn->src);
			break;
		}

		/* other operations, TODO: can be commented out! */
		case MKDIR:
		case RMDIR:
		case SYMLINK:
		case CHMOD:
		case CHOWN:
		case UTIMENS:
			break;

		/* error operations */
		default:
			fprintf(stderr, "error operation: %d\n", sn->op);
			break;
	}
	
	return 0;
}


