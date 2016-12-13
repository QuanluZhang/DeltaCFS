#ifndef _SYNCDELTA_H_
#define _SYNCDELTA_H_

#include <stdio.h>
#include <fuse.h>
#include <string.h>
#include <time.h>
#include <librsync.h>
#include <sys/stat.h>
#include "deltacfs_client_gearman.h"

// Note: the computation triggers pack of write buffer!!!
int compute_delta(char *originfile, char *newfile, char *basefile, char *resfile, char *file_delta_path);

#endif
