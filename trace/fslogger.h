#ifndef FSLOG_H
#define FSLOG_H

// #include <Windows.h>
// #include "dokan.h"
#include <stdint.h>
using namespace std;

#define FS_OPERATIONTYPE unsigned char
#define OPERATION_RESERVE 0
#define OPERATION_CREATEFILE 1
#define OPERATION_CREATEDIR 2
#define OPERATION_DELETEFILE 3
#define OPERATION_MOVEFILE 4
#define OPERATION_WRITEFILE 5
#define OPERATION_SETENDOFFILE 6
#define OPERATION_FLUSHFILEBUFFERS 7
#define OPERATION_BACKUPFILE 8

//#define OPERATION_READFILE

#define FS_PATHLENGTH unsigned short
#define MAX_PATH_UTF 1560 //=6*MAX_PATH

// int LoggerCreateFile(LPCWSTR FileName);

// int LoggerCreateDirectory(LPCWSTR FileName);

// int LoggerDeleteFile(LPCWSTR FileName);

// int LoggerMoveFile(LPCWSTR FileName, LPCWSTR newFileName);

// int LoggerWriteFile(LPCWSTR FileName, DWORD NumberOfBytesWritten, LPCVOID buffer, LONGLONG offset);

// int LoggerSetEndOfFile(LPCWSTR FileName, LARGE_INTEGER offset);

// int LoggerFlushFileBuffers(LPCWSTR FileName);

//int LoggerReadFile(LPCWSTR FileName, DWORD NumberOfBytesWritten, LONGLONG offset);

//Init
// int LoggerInit(char *logfilename, LPCWSTR backuppath);

// int LoggerBackupFile(LPCWSTR FileName, LPCWSTR filePath);

#endif








