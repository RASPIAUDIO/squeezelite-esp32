/* MnCast Wi-Fi recovery screen. QR encoder: Nayuki, MIT (qr/). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "wifi_setup.h"
#include "gds.h"
#include "gds_draw.h"
#include "gds_font.h"
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
            GDS_DrawBox(d,x,y,x+side-1,y+side-1,GDS_COLOR_WHITE,true);
            for(int r=0;r<n;r++) for(int c=0;c<n;c++) if(qrcodegen_getModule(qr,c,r))
                GDS_DrawBox(d,x+(c+4)*scale,y+(r+4)*scale,x+(c+5)*scale-1,y+(r+5)*scale-1,GDS_COLOR_BLACK,true);
        }
    }
    free(temp);free(qr);return ok;
}
static int wrapped(struct GDS_Device *d, const char *prefix, const char *value, int y) {
    char text[112], line[112];
    snprintf(text,sizeof(text),"%s%s",prefix,value);
    unsigned cols=(GDS_GetWidth(d)-8)/7, len=strlen(text);
    if(cols>sizeof(line)-1) cols=sizeof(line)-1;
    for(unsigned i=0;i<len;i+=cols) {
        unsigned n=len-i<cols?len-i:cols;
        memcpy(line,text+i,n);line[n]=0;
        GDS_FontDrawString(d,4,y,line,GDS_COLOR_WHITE);y+=13;
    }
    return y;
}
bool wifi_setup_render(struct GDS_Device *d, const char *ssid, const char *password, const char *ip, bool open) {
    if(GDS_GetWidth(d)<320 || GDS_GetHeight(d)<240) return false;
    char payload[224],url[48];
    if(!wifi_setup_payload(payload,sizeof(payload),ssid,password,open)) return false;
    snprintf(url,sizeof(url),"http://%s/#tab-wifi",ip);
    const struct GDS_FontDef *previous=GDS_SetFont(d,&Font_droid_sans_mono_7x13);
    GDS_SetTextWidth(d,0);
    GDS_Clear(d,GDS_COLOR_BLACK);
    GDS_FontDrawString(d,4,0,"Wi-Fi setup / No network",GDS_COLOR_WHITE);
    GDS_FontDrawString(d,4,15,"Scan to join / Open sign-in page",GDS_COLOR_WHITE);
    int h=GDS_GetHeight(d)-103;
    bool first=qr_draw(d,payload,0,30,GDS_GetWidth(d),h);
    int y=GDS_GetHeight(d)-65;
    y=wrapped(d,"SSID: ",ssid,y);
    y=wrapped(d,"Password: ",open?"(open network)":password,y);
    y=wrapped(d,"IP: ",ip,y);
    wrapped(d,"",url,y);
    GDS_SetFont(d,previous);
    GDS_Update(d);
    return first;
}
