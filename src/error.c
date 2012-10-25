/*
    Copyright (C) 2012  MindMaze SA
    All right reserved

    Author: Nicolas Bourdaud <nicolas.bourdaud@mindmaze.ch>
*/
#if HAVE_CONFIG_H
# include <config.h>
#endif

#include "mmerrno.h"
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include "nls-internals.h"

struct errmsg_entry {
	int errnum;
	const char* msg;
};

/**************************************************************************
 *                                                                        *
 *                       Messages definition                              *
 *                                                                        *
 **************************************************************************/
static const struct errmsg_entry error_tab[] = {
	{.errnum = MM_EDISCONNECTED,
	 .msg = N_("The acquisition module has been disconnected.")},
};

#define NUM_ERROR_ENTRY	(sizeof(error_tab)/sizeof(error_tab[0]))

/**************************************************************************
 *                                                                        *
 *                           Implementation                               *
 *                                                                        *
 **************************************************************************/
static pthread_once_t translation_is_initialized = PTHREAD_ONCE_INIT;


static
void init_translation(void)
{
	bindtextdomain(PACKAGE_NAME, LOCALEDIR);
}


static
const char* get_mm_errmsg(int errnum)
{
	const char* msg;
	int i = errnum - error_tab[0].errnum;
	
	pthread_once(&translation_is_initialized, init_translation);

	msg = error_tab[i].msg;

	return dgettext(PACKAGE_NAME, msg); 
}


API_EXPORTED
const char* mmstrerror(int errnum)
{
	if ((errnum < error_tab[0].errnum)
	  || (errnum > error_tab[NUM_ERROR_ENTRY-1].errnum))
		return strerror(errnum);

	return get_mm_errmsg(errnum);
}


API_EXPORTED
int mmstrerror_r(int errnum, char *buf, size_t buflen)
{
	const char* msg;
	size_t msglen, trunclen;

	if ((errnum < error_tab[0].errnum)
	  || (errnum > error_tab[NUM_ERROR_ENTRY-1].errnum))
		return strerror_r(errnum, buf, buflen);

	if (buflen < 1) {
		errno = ERANGE;
		return -1;
	}

	msg = get_mm_errmsg(errnum);

	msglen = strlen(msg)+1;
	trunclen = (buflen < msglen) ? buflen : msglen;
	memcpy(buf, msg, trunclen-1);
	buf[trunclen-1] = '\0';

	if (trunclen != msglen) {
		errno = ERANGE;
		return -1;
	}

	return 0;
}
