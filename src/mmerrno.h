/*
    Copyright (C) 2012  MindMaze SA
    All right reserved

    Author: Nicolas Bourdaud <nicolas.bourdaud@mindmaze.ch>
*/
#ifndef MMERRNO_H
#define MMERRNO_H

#include <stddef.h>
#include <errno.h>

#define MM_EDISCONNECTED	1000
#define MM_EUNKNOWNUSER		1001
#define MM_EWRONGPWD		1002

const char* mmstrerror(int errnum);
int mmstrerror_r(int errnum, char *buf, size_t buflen);

#endif /* MMERRNO */
