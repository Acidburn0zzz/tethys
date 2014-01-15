/* Tethys, cookie.c -- unique, comparable cookies
   Copyright (C) 2013 Alex Iadicicco

   This file is protected under the terms contained
   in the COPYING file in the project root */

#include "ircd.h"

void u_cookie_reset(u_cookie *ck)
{
	ck->high = ck->low = 0;
}

void u_cookie_inc(u_cookie *ck)
{
	if (ck->high == NOW.tv_sec) {
		ck->low ++;
		return;
	}

	ck->high = NOW.tv_sec;
	ck->low = 0;
}

void u_cookie_cpy(u_cookie *a, u_cookie *b)
{
	memcpy(a, b, sizeof(*a));
}

static int norm(int x)
{
	if (x < 0) return -1;
	if (x > 0) return 1;
	return 0;
}

int u_cookie_cmp(u_cookie *a, u_cookie *b)
{
	if (a->high == b->high)
		return norm(a->low - b->low);
	return norm(a->high - b->high);
}
