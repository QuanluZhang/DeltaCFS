#include <cstdio>
#include "fslogger.h"
#include <map>
#include <string>
#include <unistd.h>
#include <sys/types.h>
#include <cstring>
#include <cstdlib>
#include <sys/select.h>
#include <ctime>
using namespace std;

// Max interval time between two opreations, to speed up replay.
int maxinterval=3*100000; //3 seconds

FILE *fp;
// Time in the trace uses 4+1 bytes due to some API used in the recorder.
unsigned char timelow;
unsigned int timehigh;
long long nexttime,nowtime,timeoffset;
FS_OPERATIONTYPE op;
map<string,FILE*> filep;
string pathprefix;
char replayoffset=0;

string readFileName() {
	FS_PATHLENGTH FileNameLen;
	char FileNamemb[MAX_PATH_UTF];
	fread(&FileNameLen, sizeof(FileNameLen), 1, fp);
	fread(FileNamemb, sizeof(char), FileNameLen, fp);
    int i;
    // Since traces can be recorded in Windows.
    if (FileNamemb[0]=='\\') FileNamemb[0]='/';
    // The replayer can't handle directories well; and like Amazon S3,
    // directories in our system are treated as path prefix.
    for (i=1;i<strlen(FileNamemb);i++) if ((FileNamemb[i]=='\\')||(FileNamemb[i]==' ')) FileNamemb[i]='_';
    // TODO:change this in the trace instead of in the replayer.
    if (strstr(FileNamemb,"testlarge")!=NULL) FileNamemb[1]='~';
    // Dropbox may need "~$" prefix on temporary files.
    if (FileNamemb[1]=='~') FileNamemb[2]='$';
	printf("%s", FileNamemb);
    return pathprefix+(string)FileNamemb;
}

void closefile(string name) {
	if (filep.find(name)!=filep.end()) {
		FILE *t=filep.find(name)->second;
		fclose(t);
	}
	filep.erase(name);
}

void closeallfile() {
    map<string,FILE*>::iterator i;
	for (i=filep.begin();i!=filep.end();++i) {
		FILE *t=i->second;
		fclose(t);
	}
	filep.clear();
}

void openfile(string name) {
	if (filep.find(name)==filep.end()) {
		filep[name]=fopen(name.c_str(),"rb+");
		if (filep[name]==NULL)
		filep[name]=fopen(name.c_str(),"wb+");
	}
}

void createfile(string name) {
	closefile(name);
	filep[name]=fopen(name.c_str(),"wb+");
}

void deletefile(string name) {
    openfile(name);
	closefile(name);
	remove(name.c_str());
}

void movefile(string name,string name2) {
    openfile(name);
	closefile(name);
	closefile(name2);
	rename(name.c_str(),name2.c_str());
}

void writefile(string name,void *buffer,uint32_t length,int64_t offset) {
    int res;
	openfile(name);
	res=fseek(filep[name],(long int)offset,SEEK_SET);
    if (res) {
        printf("writefile(): fseek() error %d\n",res);
    }
    for (int i=0;i<length;i++)
        ((char*)buffer)[i]+=replayoffset;
	fwrite(buffer,1,length,filep[name]);
	fflush(filep[name]);
}

void truncate(string name,int64_t offset) {
    openfile(name);
	closefile(name);
	truncate(name.c_str(),(off_t)offset);
}

int main(int argc, char** argv) {
    if (argc<4) {
        printf("Usage: %s logfile pathprefix maxinterval replayoffset\n",argv[0]);
        printf("Example %s a.log /tmp/test 6 0\n",argv[0]);
        printf(
"logfile: the log filename;\n"
"pathprefix: the replay path (without the ending /);\n"
"maxinterval: max interval (in seconds) between operations;\n"
"replayoffset: an offset added to each byte written to disk to bypass Dropbox's duplicate detection.\n");
        return 0;
    }
	fp=fopen(argv[1], "rb");
    pathprefix=(string)argv[2];
    if (argc>2) {
        maxinterval=atoi(argv[3])*100000;
    }
    if (argc>4) {
        replayoffset=atoi(argv[4]);
    }
	uint32_t length;
	int64_t offset;
	string name,name2;
	void *buffer;
    nowtime=nexttime=0;
    timeoffset=-1000000000000LL;
    timespec nowtimespec;
    timeval pauseval;
	while (!feof(fp)) {
		fread(&timelow, sizeof(timelow), 1, fp);
		fread(&timehigh, sizeof(timehigh), 1, fp);
        nexttime=(timehigh*100+timelow); //per tick=10us=0.01ms=0.00001s
        clock_gettime(CLOCK_MONOTONIC,&nowtimespec);
        nowtime=nowtimespec.tv_sec*100000+nowtimespec.tv_nsec/10000;
        if (nowtime+timeoffset+maxinterval<nexttime) {
            closeallfile();
            timeoffset=nexttime-nowtime-maxinterval;
        }
        while (nowtime+timeoffset<nexttime) {
            pauseval.tv_sec=(nexttime-nowtime-timeoffset)/100000;
			pauseval.tv_usec=((nexttime-nowtime-timeoffset)%100)*10;
            select(0,NULL,NULL,NULL,&pauseval);
            clock_gettime(CLOCK_MONOTONIC,&nowtimespec);
            nowtime=nowtimespec.tv_sec*100000+nowtimespec.tv_nsec/10000;
        }
		fread(&op, sizeof(op), 1, fp);
		if (feof(fp)) break;
		printf("%d.%02d ", timehigh, timelow);
		switch (op) {
		case OPERATION_CREATEFILE:
			printf("CreateFile: ");
			name=readFileName();
            if (name.find("RD3850")==string::npos) createfile(name);
            printf("...done!\n");
			break;
		case OPERATION_CREATEDIR:
			printf("CreateDirectory: ");
			name=readFileName();
            //if (name.find("RD3850")!=string::npos) break;
			printf("\n");
			//do nothing
			break;
		case OPERATION_DELETEFILE:
			printf("DeleteFile: ");
			name=readFileName();
            if (name.find("RD3850")==string::npos) deletefile(name);
            printf("...done!\n");
			break;
		case OPERATION_MOVEFILE:
			printf("MoveFile: from ");
			name=readFileName();
			printf(" to ");
			name2=readFileName();
			if ((name.find("RD3850")==string::npos)&&(name2.find("RD3850")==string::npos)) movefile(name,name2);
            printf("...done!\n");
			break;
		case OPERATION_WRITEFILE:
			printf("WriteFile: ");
			name=readFileName();
            fread(&length, sizeof(length), 1, fp);
			buffer=malloc(length);
			//fseek(fp, length, SEEK_CUR);
			fread(buffer,1,length,fp);
			fread(&offset, sizeof(offset), 1, fp);
			printf(" Length=%d Offset=%lld", length, offset);
			if (name.find("RD3850")==string::npos) writefile(name,buffer,length,offset);
			free(buffer);
            printf("...done!\n");
			break;
		case OPERATION_SETENDOFFILE:
			printf("SetEndOfFile: ");
			name=readFileName();
            fread(&offset, sizeof(offset), 1, fp);
			printf(" Offset=%lld", offset);
			if (name.find("RD3850")==string::npos) truncate(name,offset);
            printf("...done!\n");
			break;
		case OPERATION_FLUSHFILEBUFFERS:
			printf("FlushFileBuffers: ");
			name=readFileName();
            if (name.find("RD3850")==string::npos) closefile(name);
			printf("...done!\n");
			//fsync?
			break;
		case OPERATION_BACKUPFILE:
			printf("BackupFile from: ");
			readFileName();
			printf(" to ");
			readFileName();
			printf("\n");
			//do nothing
			break;
		default:
			printf("ERROR!\n");
			return 0;
		}
	}
    printf("Success!\n");
	return 0;
}
