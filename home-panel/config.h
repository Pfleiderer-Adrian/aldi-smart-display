// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PANEL_CONFIG_H
#define PANEL_CONFIG_H
struct panel_config {char host[64],token[2048],dashboard[81],view[81],ssid[33],password[65];unsigned port;};
int config_load(const char *path,struct panel_config *config);
int config_wifi(const struct panel_config *config,const char *path);
#endif
