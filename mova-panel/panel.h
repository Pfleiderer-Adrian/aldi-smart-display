// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef MOVA_PANEL_H
#define MOVA_PANEL_H
#include <stddef.h>
#define JSMN_HEADER
#include "vendor/jsmn.h"
#undef JSMN_HEADER
struct room {int id;char name[64];};
struct config {int demo,count,map_id,suction,water,cleaning_mode;char map_stamp[33];char user[256],pass[256],region[4],did[80],model[80],ssid[33],wifi[65];struct room rooms[32];};
int config_load(const char*,struct config*);
int config_wifi(const struct config*,const char*);
int safe_id(const char*);
int json_parse(const char*,jsmntok_t*,int);
int json_get(const char*,jsmntok_t*,int,int,const char*);
int json_int(const char*,jsmntok_t*,int,int,const char*,int);
int json_string(const char*,jsmntok_t*,int,int,const char*,char*,size_t);
int cloud_discover(const struct config*);
int cloud_sign(const char*,long long,char*,size_t);
int sync_time(void);
int cloud_login(const struct config*);
int cloud_start(const struct config*,unsigned,int);
int cloud_pause(const struct config*);
int cloud_stop(const struct config*);
int cloud_inspect(const struct config*);
int cloud_map_list(const struct config*,char*,size_t);
int cloud_probe(const struct config*);
int command_params(const struct config*,unsigned,int,char*,size_t);
int rooms_decode(const char*,struct config*);
int cloud_rooms(struct config*);
int custom_mode_params(const struct config*,char*,size_t);
int cleaning_settings_decode(const char*,struct config*);
int cloud_plan(struct config*,unsigned,int);
#endif
