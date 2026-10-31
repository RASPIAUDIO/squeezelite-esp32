#pragma once
#include <stdbool.h>
struct GDS_Device;
bool wifi_setup_render(struct GDS_Device *display, const char *ssid, const char *password, const char *ip, bool open);
bool wifi_setup_payload(char *out, unsigned capacity, const char *ssid, const char *password, bool open);
