/* MnCast Wi-Fi recovery screen. QR encoder: Nayuki, MIT (qr/). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "wifi_setup.h"
#include "gds.h"
#include "gds_draw.h"
#include "gds_font.h"
#include "gds_private.h"
#include "qr/qrcodegen.h"

static bool escape(char *out, unsigned cap, const char *in) {
    unsigned n = 0;
    for (; *in; in++) {
        if (strchr("\\;,:\"", *in)) { if (n + 2 >= cap) return false; out[n++] = '\\'; }
        if (n + 1 >= cap) return false;
        out[n++] = *in;
    }
    out[n] = 0; return true;
}
bool wifi_setup_payload(char *out, unsigned capacity, const char *ssid, const char *password, bool open) {
    char s[65], p[129];
    if (!out || !ssid || !password || strlen(ssid)>32 || strlen(password)>64 ||
        !escape(s,sizeof(s),ssid) || !escape(p,sizeof(p),password)) return false;
    int n = snprintf(out,capacity,"WIFI:T:%s;S:%s;P:%s;;",open?"nopass":"WPA",s,open?"":p);
    return n >= 0 && (unsigned)n < capacity;
}
/* DrawBox uses an exclusive right edge. Keep QR modules exact, including
 * their last column, without changing that shared legacy API. */
static void rect(struct GDS_Device *d, int x, int y, int w, int h, int color) {
    for (int row=y; row<y+h; row++) GDS_DrawHLine(d,x,row,w,color);
}
static bool qr_draw(struct GDS_Device *d, const char *payload, int left, int top, int width, int height) {
    const unsigned cap = qrcodegen_BUFFER_LEN_FOR_VERSION(10);
    uint8_t *temp = malloc(cap), *qr = malloc(cap);
    bool ok = temp && qr && qrcodegen_encodeText(payload,temp,qr,qrcodegen_Ecc_LOW,1,10,qrcodegen_Mask_AUTO,true);
    if (ok) {
        int n=qrcodegen_getSize(qr), scale=width/(n+8);
        if (scale>height/(n+8)) scale=height/(n+8);
        if (scale<2) ok=false;
        else {
            int side=(n+8)*scale, x=left+(width-side)/2, y=top+(height-side)/2;
            rect(d,x,y,side,side,GDS_COLOR_WHITE);
            for(int r=0;r<n;r++) for(int c=0;c<n;c++) if(qrcodegen_getModule(qr,c,r))
                rect(d,x+(c+4)*scale,y+(r+4)*scale,scale,scale,GDS_COLOR_BLACK);
        }
    }
    free(temp);free(qr);return ok;
}
static int wrapped(struct GDS_Device *d, const char *text, int y, unsigned cols) {
    char line[112];
    unsigned len=strlen(text);
    for(unsigned i=0;i<len;i+=cols) {
        unsigned n=len-i<cols?len-i:cols;
        memcpy(line,text+i,n);line[n]=0;
        GDS_FontDrawString(d,12,y,line,GDS_COLOR_WHITE);y+=13;
    }
    return y;
}
bool wifi_setup_render(struct GDS_Device *d, const char *ssid, const char *password, const char *ip, bool open) {
    if(!d || !ip || GDS_GetWidth(d)<320 || GDS_GetHeight(d)<240) return false;
    char payload[224],url[48];
    if(!wifi_setup_payload(payload,sizeof(payload),ssid,password,open)) return false;
    snprintf(url,sizeof(url),"http://%s/#tab-wifi",ip);
    char fields[4][112];
    snprintf(fields[0],sizeof(fields[0]),"SSID: %s",ssid);
    snprintf(fields[1],sizeof(fields[1]),"Password: %s",open?"(open network)":password);
    snprintf(fields[2],sizeof(fields[2]),"IP: %s",ip);
    snprintf(fields[3],sizeof(fields[3]),"%s",url);
    unsigned cols=(GDS_GetWidth(d)-24)/7, rows=0;
    if(cols>111) cols=111;
    for(int i=0;i<4;i++) rows+=(strlen(fields[i])+cols-1)/cols;
    int footer=GDS_GetHeight(d)-10-13*rows;
    const struct GDS_FontDef *previous=d->Font;
    int text_width=d->TextWidth;
    bool proportional=d->FontForceProportional, monospace=d->FontForceMonospace;
    GDS_SetFont(d,&Font_droid_sans_mono_7x13);
    GDS_SetTextWidth(d,0);
    GDS_Clear(d,GDS_COLOR_BLACK);
    /* RASPIAUDIO's #4064d7 primary blue; monochrome displays remain legible. */
    int blue=GDS_GetMode(d)==GDS_RGB565 ? 0x433a :
             GDS_GetMode(d)==GDS_RGB888 ? 0x4064d7 : GDS_COLOR_BLACK;
    rect(d,8,6,GDS_GetWidth(d)-16,24,blue);
    GDS_FontDrawString(d,14,11,"SqueezeLite ESP32-S31",GDS_COLOR_WHITE);
    GDS_FontDrawString(d,GDS_GetWidth(d)-91,11,"WI-FI SETUP",GDS_COLOR_WHITE);
    int split=GDS_GetWidth(d)/2;
    bool first=qr_draw(d,payload,split,34,GDS_GetWidth(d)-split-12,footer-40);
    GDS_FontDrawString(d,12,48,"Let's connect",GDS_COLOR_WHITE);
    GDS_FontDrawString(d,12,76,"1. Scan the QR",GDS_COLOR_WHITE);
    GDS_FontDrawString(d,12,92,"2. Join Wi-Fi",GDS_COLOR_WHITE);
    GDS_FontDrawString(d,12,116,"Open sign-in",GDS_COLOR_WHITE);
    GDS_FontDrawString(d,12,132,"page to finish",GDS_COLOR_WHITE);
    rect(d,12,footer-5,GDS_GetWidth(d)-24,1,blue);
    int y=footer;
    for(int i=0;i<4;i++) y=wrapped(d,fields[i],y,cols);
    GDS_SetFont(d,previous);
    d->FontForceProportional=proportional;
    d->FontForceMonospace=monospace;
    GDS_SetTextWidth(d,text_width);
    GDS_Update(d);
    return first;
}
