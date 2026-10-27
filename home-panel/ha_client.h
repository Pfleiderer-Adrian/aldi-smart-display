// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef HA_CLIENT_H
#define HA_CLIENT_H
#include <pthread.h>
struct tile { char name[81], value[81], unit[31], entity[121];
 char kind[32], hint[81], action[1024];float minimum,maximum;int explicit_name;int volume_configured,volume_available;float volume;int brightness_configured,brightness_available;float brightness; };
struct dashboard_model { struct tile tiles[64];unsigned count;char title[81],view[81],path[81];
 char view_paths[16][81];unsigned view_count;int overflow; };
int dashboard_parse(const char *json,const char *dashboard,const char *view,struct dashboard_model *model);
void dashboard_attributes(const char *json,char *name,unsigned n,char *unit,unsigned u);
int json_success(const char *json);
int dashboard_title_from_list(const char *json,const char *path,char *out,unsigned capacity);
void ha_snapshot(struct tile *out,unsigned *count,char *title,unsigned capacity);
void ha_reload(void);
void ha_next_view(void);
void ha_view_position(unsigned *current,unsigned *total);
void ha_tap(unsigned index,const struct tile *expected);
int dashboard_volume(const char *json,float *level);
int dashboard_brightness(const char *json,float *level);
void ha_brightness(unsigned index,const struct tile *expected,float value);
void ha_volume(unsigned index,const struct tile *expected,float value);
void ha_notice(char *out,unsigned capacity);

extern pthread_mutex_t ha_mutex;
int ha_start(const char *config,struct tile *tiles,unsigned count);
void ha_status(char *out,unsigned size);
void ha_stop(void);
int ha_probe(const char *config,struct tile *tiles,unsigned count);
int ha_parse_state(const char *json,char *value,unsigned size);
#endif
