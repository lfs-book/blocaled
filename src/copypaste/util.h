/*-*- Mode: C; c-basic-offset: 8; indent-tabs-mode: nil -*-*/

#pragma once

/***
  This file is part of systemd.

  Copyright 2010 Lennart Poettering

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#include <inttypes.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sched.h>
#include <limits.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/resource.h>

#include "macro.h"

typedef uint64_t usec_t;
typedef uint64_t nsec_t;

typedef struct dual_timestamp {
        usec_t realtime;
        usec_t monotonic;
} dual_timestamp;

#define MSEC_PER_SEC  1000ULL
#define USEC_PER_SEC  1000000ULL
#define USEC_PER_MSEC 1000ULL
#define NSEC_PER_SEC  1000000000ULL
#define NSEC_PER_MSEC 1000000ULL
#define NSEC_PER_USEC 1000ULL

#define USEC_PER_MINUTE (60ULL*USEC_PER_SEC)
#define NSEC_PER_MINUTE (60ULL*NSEC_PER_SEC)
#define USEC_PER_HOUR (60ULL*USEC_PER_MINUTE)
#define NSEC_PER_HOUR (60ULL*NSEC_PER_MINUTE)
#define USEC_PER_DAY (24ULL*USEC_PER_HOUR)
#define NSEC_PER_DAY (24ULL*NSEC_PER_HOUR)
#define USEC_PER_WEEK (7ULL*USEC_PER_DAY)
#define NSEC_PER_WEEK (7ULL*NSEC_PER_DAY)
#define USEC_PER_MONTH (2629800ULL*USEC_PER_SEC)
#define NSEC_PER_MONTH (2629800ULL*NSEC_PER_SEC)
#define USEC_PER_YEAR (31557600ULL*USEC_PER_SEC)
#define NSEC_PER_YEAR (31557600ULL*NSEC_PER_SEC)

/* What is interpreted as whitespace? */
#define WHITESPACE " \t\n\r"
#define NEWLINE "\n\r"
#define QUOTES "\"\'"
#define COMMENTS "#;\n"

usec_t now(clockid_t clock);

usec_t timespec_load(const struct timespec *ts);

#define streq(a,b) (strcmp((a),(b)) == 0)
#define strneq(a, b, n) (strncmp((a), (b), (n)) == 0)

#define new(t, n) ((t*) malloc(sizeof(t)*(n)))

#define new0(t, n) ((t*) calloc((n), sizeof(t)))

#define newa(t, n) ((t*) alloca(sizeof(t)*(n)))

#define newdup(t, p, n) ((t*) memdup(p, sizeof(t)*(n)))

#define malloc0(n) (calloc((n), 1))

int close_nointr(int fd);
void close_nointr_nofail(int fd);
void close_many(const int fds[], unsigned n_fd);

int parse_boolean(const char *v);

int read_one_line_file(const char *fn, char **line);

char *strappend(const char *s, const char *suffix);
char *strnappend(const char *s, const char *suffix, size_t length);

char *strstrip(char *s);
char *truncate_nl(char *s);

bool ignore_file(const char *filename);

char *strjoin(const char *x, ...) _sentinel_;

extern int saved_argc;
extern char **saved_argv;
