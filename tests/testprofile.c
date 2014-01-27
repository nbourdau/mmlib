/*
   @mindmaze_header@
*/
#if HAVE_CONFIG_H
# include <config.h>
#endif


#include "mmprofile.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

#define OUTFD	STDOUT_FILENO

static
void print_profile(void)
{
	int i, j;
	volatile int x;

	for (i = 0; i < 100; i++) {
		x = 2;
		mmtic();
		x /= 2;
		if (x == 1)
			x /= 10;
		mmtoc();
		for (j = 0; j < 10; j++)
			x *= 2;
		mmtoc();
		x = 2;
		for (j = 0; j < 50; j++)
			x *= 2;
		mmtoc();
	}

	mmprofile_print(PROF_MEAN|PROF_MIN|PROF_MAX, OUTFD);
}


int main(void)
{
	dprintf(OUTFD, "Timing with default settings\n");
	print_profile();

	dprintf(OUTFD, "\nTiming with wall clock timer\n");
	mmprofile_reset(0);
	print_profile();

	dprintf(OUTFD, "\nTiming with CPU based timer\n");
	mmprofile_reset(1);
	print_profile();

	return EXIT_SUCCESS;
}
