#ifndef _SYNCUTIL_H_
#define _SYNCUTIL_H_

#include <fuse.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "deltacfs_client_gearman.h"


void PrintHex(char* ptr, int cnt);

void find_filename(char *path, char *filename);

void printfile(char *path);

void create_tmp(const char *path, char *tmp_path);

void rename_tmp(const char *path, char *tmp_path);

int copyfile(const char* src, char* dest);

long get_file_size(const char *path);

char *dict(int op);

#endif
