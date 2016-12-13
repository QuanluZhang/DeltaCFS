#include "syncdelta.h"

extern void find_filename(char *path, char *filename);
extern char TMP_SIG[30];
extern char TMP_DELTA[30];

/*
   Given the originfile and newfile, generating a delta and patch to originfile(at the server side anyway)
	para1: originfile: an original file
	newfile: new version file
	basefile: file to be patched with delta on server
	resfile: newly generated file on server
	return delta file name
   */
int compute_delta(char *originfile, char *newfile, char *basefile, char *resfile, char *file_delta_path)
{
	// generate signature
	rs_stats_t stats;
	rs_result rsyncresult;
	rs_signature_t *sumset;
	char filename[NAME_LEN];
	char resname[NAME_LEN];
	char file_sig_path[PATH_LEN];
	strcpy(file_sig_path, TMP_SIG);
	//char *file_delta_path = (char *) malloc (PATH_LEN * sizeof(char));
	strcpy(file_delta_path, TMP_DELTA);
	FILE *ofile, *sigfile, *deltafile, *nfile;
	
	printf("compute_delta:%s %s %s %s\n", originfile, newfile, basefile, resfile);
	ofile = fopen(originfile, "rb");

	if(ofile == NULL)
	{
		printf("origin file %s open error.\n", originfile);
		return 1;
	}
	find_filename(basefile, filename);
	find_filename(resfile, resname);
	strcat(file_sig_path, filename);

	sigfile = fopen(file_sig_path, "wb+");
	rsyncresult = rs_sig_file(ofile, sigfile, RS_DEFAULT_BLOCK_LEN, RS_DEFAULT_STRONG_LEN, &stats);
	
	if (rsyncresult != RS_DONE) {
		printf("generate signature Failed!\n");
		return 1;
	}
	rs_log_stats(&stats);

	// load sig file
	fseek(sigfile, 0, 0);
	rsyncresult = rs_loadsig_file(sigfile, &sumset, &stats, ofile);
	//rsyncresult = rs_loadsig_file(sigfile, &sumset, &stats);
	
	// fix a bug
	//fclose(ofile);
	fclose(sigfile);
	remove(file_sig_path);
	
	if (rsyncresult != RS_DONE) {
		printf("load signature into librsync Failed!\n");
		return 1;
	}
	rsyncresult = rs_build_hash_table(sumset);
	
	if (rsyncresult != RS_DONE) {
		printf("build hash table in librsync Failed!\n");
        rs_free_sumset(sumset);
		return 1;
	}
	rs_log_stats(&stats);

	// compute delta
	strcat(file_delta_path, filename);
	char rand_c[4] = {'\0'};
	rand_c[0] = (char)(rand() % 26 + 97);
	rand_c[1] = (char)(rand() % 26 + 97);
	rand_c[2] = (char)(rand() % 26 + 97);
	strcat(file_delta_path, rand_c);
	deltafile = fopen(file_delta_path, "wb+");
	nfile = fopen(newfile,"rb");
	if(nfile == NULL)
	{
		printf("new file %s open error.\n", newfile);
		return 1;
	}
	rsyncresult = rs_delta_file(sumset, nfile, deltafile, &stats);
	fclose(ofile);
	fclose(nfile);
	fclose(deltafile);
		
	if (rsyncresult != RS_DONE) {
		printf("delta calculation Failed!\n");
        rs_free_sumset(sumset);
		return 1;
	}
	rs_log_stats(&stats);
	rs_free_sumset(sumset);
    
	return 0;
}


