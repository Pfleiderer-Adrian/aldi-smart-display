// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef WS_CLIENT_H
#define WS_CLIENT_H
#include <stddef.h>
int ws_request(const char *host,unsigned port,const char *token,const char *request,char *response,size_t capacity);
#endif
