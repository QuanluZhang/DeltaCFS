#include "syncutil.h"
#include <unistd.h>

//extern newfile_hash *nhs;
extern char TMP_DIR[30];
void PrintHex(char* ptr, int cnt)
{
	int i;
	for (i = 0; i < cnt; i++)
	{
		printf("%x ", ptr[i]);
	}
	printf("\n");
}

// some functional tools

void find_filename(char *path, char *filename)
{
	char *p;
	char delim[] = "/";
	//char space[] = " ";
	char tpath[PATH_LEN];
	strcpy(tpath, path);
	p = strtok(tpath, delim);
	strcpy(filename, p);
	while((p=strtok(NULL, delim)))
		strcpy(filename, p);
}

void printfile(char *path)
{
    char *buf;
    int filesize;
    struct stat statbuff;
    //int res;
    int fd;
    fd = open(path, O_RDONLY);
    if(stat(path, &statbuff) >= 0)
    {
        filesize = statbuff.st_size;
        buf = malloc(sizeof(char)*(filesize+10));
		memset(buf, '\0', filesize+10);
        //res = pread(fd, buf, filesize, 0);
        pread(fd, buf, filesize, 0);
        printf("buf =\n%s\n", buf);
    }
	close(fd);
}

// Note: the path must include file name
void create_tmp(const char *path, char *tmp_path)
{
	strcpy(tmp_path, TMP_DIR);
	char filename[NAME_LEN];
	char *p;
	char delim[] = "/";
	//char space[] = " ";
	char tpath[PATH_LEN];
	strcpy(tpath, path);
	p = strtok(tpath, delim);
	strcpy(filename, p);
	while((p=strtok(NULL, delim)))
		strcpy(filename, p);
	strcat(tmp_path, filename);
	copyfile(path, tmp_path);
}

// Note: the path must include file name
// TODO: the files with the same name but in different folders
// cannot be properly dealt with. We can use temporary file names,
// but have to maintain the relation of file names
void rename_tmp(const char *path, char *tmp_path)
{
	strcpy(tmp_path, TMP_DIR);
	char filename[NAME_LEN];
	char *p;
	char delim[] = "/";
	//char space[] = " ";
	char tpath[PATH_LEN];
	strcpy(tpath, path);
	p = strtok(tpath, delim);
	strcpy(filename, p);
	while((p=strtok(NULL, delim)))
		strcpy(filename, p);
	strcat(tmp_path, filename);
}
int copyfile(const char* src, char* dest)
{
	FILE *fp1, *fp2;
	int factread;
	long fsize, file_size;
	unsigned char buffer[FILE_LEN];
 
	fp1 = fopen(src,"rb");
	fp2 = fopen(dest,"wb+");
	if (!fp1 || !fp2) return 1;

	file_size = get_file_size(src);
	
	for (fsize=file_size; fsize>0; fsize-=FILE_LEN)
	{
		factread = fread(buffer, 1, FILE_LEN, fp1);
	    fwrite(buffer, factread, 1, fp2);
	}
	fclose(fp1);
	fclose(fp2);
	return 0;
}

long get_file_size(const char *path)
{
	struct stat statbuff;
	if(stat(path, &statbuff) >= 0)
	{
		return statbuff.st_size;
	}
	else 
	{
		printf("get file size error\n");
		return -1;
	}
}

char *dict(int op)
{
	switch(op)
	{
		case 0: return "mknod";
		case 1: return "mkdir";
		case 2: return "symlink";
		case 3: return "rmdir";
		case 4: return "link";
		case 5: return "chmod";
		case 6: return "chown";
		case 7: return "truncate";
		case 8: return "utimens";
		case 9: return "rename";
		case 10: return "unlink";
		case 11: return "newfile";
		case 12: return "write";
		case 13: return "delta";
	    default: return NULL;
	}
}

