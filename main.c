/*
 *	Author: Hiroshi Nishida, nishida@asusa.net
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <getopt.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#define FUSE_USE_VERSION	30 // Must be defined before fuse.h
#include <fuse.h>
#include <curl/curl.h>
#define _FUSETEST_MAIN_
#include "fusetest.h"
#include "fusefs.h"
#include "log.h"
#ifdef EXTERN
#undef EXTERN
#endif
#include "util.h"

/************************************************************
	Functions
************************************************************/

// Usage
static void
Usage()
{
	fprintf(stderr, "Usage: %s [-fhv]\n"
		"-d: Output debug messages.\n"
		"-h: Show help,\n"
		"-v: Output verbose messages\n",
		Program);
	exit(1);
}

/* At exit -- usually a process with FUSE must be stopped by killing it. In
   that case, FUSE does not unmount a filesystem, so we must manually do it. */
static void
AtExit()
{
	// Unmount 
	fuse_unmount(FUSE_MNT_DIR, NULL);
}

// At signal
static void
AtSignal(int sig)
{
	exit(0);
}

// Register atexit
static void
RegAtExit()
{
	// atexit
	atexit(AtExit);

	// Signals
	signal(SIGKILL, AtSignal);
	signal(SIGINT, AtSignal);
	signal(SIGABRT, AtSignal);
	signal(SIGTERM, AtSignal);
	signal(SIGFPE, SIG_IGN);
}

#if 0
// Create dummy stat file for BigBuckBunny-Full-web.mp4
static int
CreateDummyStatF(void)
{
	int		err = 0;
	FILE		*fp = NULL;
	struct stat	sb;

	// Stat VIDEO_FILE
	if (stat(VIDEO_FILE, &sb) == -1) {
		Log("Error: %s: stat %s: %s",
			__func__, VIDEO_FILE, strerror(errno));
		err = errno;
		goto END;
	}

	// Change owner to www:www
	sb.st_uid = sb.st_gid = 80; // www:www

	// Open DUMMY_STAT_FILE
	if ((fp = fopen(DUMMY_STAT_FILE, "wb")) == NULL) {
		Log("Error: %s: fopen %s: %s",
			__func__, DUMMY_STAT_FILE, strerror(errno));
		err = errno;
		goto END;
	}

	// Wrote sb
	fwrite(&sb, 1, sizeof(struct stat), fp);

END:
	if (fp != NULL) {
		fclose(fp);
	}

	return err ? -1 : 0;
}
#endif

// Write downloaded data
static size_t
WriteDownloadedData(void *data, size_t size, size_t nmemb, void *fp)
{
	return fwrite(data, size, nmemb, (FILE *)fp);
}

// Download VIDEO_FILE
static int
DownloadVideo(void)
{
	int		err = 0;
	struct stat	sb;
	FILE		*fp = NULL;
	CURL		*curl = NULL;
	CURLcode	res;

	// Check if VIDEO_FILE exists
	if (stat(VIDEO_FILE, &sb) == 0) { // Exists
		return 0;
	}

	// Open file for writing
	if ((fp = fopen(VIDEO_FILE, "w+")) == NULL) {
		Log("Error: %s: fopen %s: %s",
			__func__, VIDEO_FILE, strerror(errno));
		err = errno;
		goto END;
	}

	// Global initialization for curl
	curl_global_init(CURL_GLOBAL_DEFAULT);

	// Initialize curl for this file
	if ((curl = curl_easy_init()) == NULL) {
		Log("Error: %s: curl_easy_init: %s", __func__, strerror(errno));
		err = errno;
		goto END;
	}
	printf("Downloading %s. Please wait...\n", VIDEO_FILE);

	// Set opt
	curl_easy_setopt(curl, CURLOPT_URL, URL);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteDownloadedData);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

	// Download
	if ((res = curl_easy_perform(curl)) != CURLE_OK) {
		Log("Error: %s: curl_easy_perform: %s",
			__func__, curl_easy_strerror(res));
		err = errno = EPERM;
		goto END;
	}
	puts("Done");

END:	// Finalize
	if (fp != NULL) {
		fclose(fp);
	}
	if (curl != NULL) {
		curl_easy_cleanup(curl);
	}

	return err ? -1 : 0;
}

// Initialize 
static void
Init(int argc, char **argv)
{
	int	ch;
	char	*p, *_p;

	// Check argv[0], and store into command
	p = argv[0];
	if ((_p = strrchr(p, '/')) != NULL) {
		p = _p + 1;
	}
	strncpy(Program, p, sizeof(Program));

	// Check argc
	if (argc < 1)
		Usage();

	// Save arguments
	Argc = argc;
	Argv = argv;

	// Initialize some parms
	Daemon = 0; // No daemon, run in foreground

	// Get options
	while ((ch = getopt(argc, argv, "dhv")) != -1) {
		switch (ch) {
		case 'd': // Debug
			Debug = 1;
			break;
		case 'v': // Verbose
			Verbose = 1;
			break;
		case 'h': // Help
		default:
			Usage();
		}
	}

	// Check argc again
	if (argc > optind) {
		Usage();
	}

	// Initialize log
	InitLog(NULL, Program, Daemon);

	// Register atexit
	RegAtExit();

	// Initialize curl
	// Download VIDEO_FILE if it doesn't exist
	if (DownloadVideo() == -1) {
		Log("Error: %s: DownloadVideo %s: %s",
			__func__, VIDEO_FILE, strerror(errno));
		exit(1);
	}

	// Create dummy stat file
	//CreateDummyStatF();
}

// Main process
static void
MainProc()
{
	char	*argv[16];
	int	i;

	// Set argv for Fuse main loop
	i = 0;
	argv[i] = Program;	i++;
	argv[i] = FUSE_MNT_DIR;	i++;
	if (!Daemon) {
		argv[i] = "-f";
		i++;
	}

	// Other options for fuse
	argv[i] = "-o";	i++;
	argv[i] = "kernel_cache,allow_other,max_write=4915200,max_read=4915200";
	//argv[i] = "sync_read,kernel_cache,allow_other,max_write=4915200,max_read=4915200";
	i++;
	/* Sync read, Allow other users to access the file, ... */
/* One of the following causes kernel crash.....
	argv[i] = "-okernel_cache"; // Disable kernel cache
	i++;
	argv[i] = "-oauto_cache"; // 
	i++;
	argv[i] = "-s"; // Single thread
	i++;
*/

	// Main loop for Fuse
	FuseLoop(i, argv);
}

// Main
int
main(int argc, char **argv)
{
	// Initialize
	Init(argc, argv);

	// Main process
	MainProc();

	exit(0);
}
