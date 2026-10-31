#include "improv_parser.h"
#include <string.h>

static const uint8_t magic[] = "IMPROV";
static void wipe(improv_parser_t *p) {
    volatile uint8_t *v = p->frame;
    for (size_t i=0; i<sizeof(p->frame); ++i) v[i]=0;
    p->used=0;
}
size_t improv_parser_read(improv_parser_t *p, void *out, size_t cap) {
    size_t n=p->text_len-p->text_pos;
    if(n>cap) n=cap;
    memcpy(out,p->text+p->text_pos,n);
    p->text_pos+=n;
    if(p->text_pos==p->text_len) p->text_pos=p->text_len=0;
    return n;
}
void improv_parser_expire(improv_parser_t *p,uint32_t now) {
    if(!p->used || (uint32_t)(now-p->last_ms)<1000) return;
    if(p->used<sizeof(magic)-1) {
        memcpy(p->text,p->frame,p->used);
        p->text_len=p->used;
    } else {
        /* A truncated credential frame remains private. Resume on newline or
         * a new IMPROV header, never echo its delayed tail into the console. */
        p->quarantine=true;
        p->resync=0;
    }
    wipe(p);
}
void improv_parser_feed(improv_parser_t *p,uint8_t b,uint32_t now,improv_frame_cb cb) {
    p->last_ms=now;
    if(p->quarantine) {
        if(b=='\n' || b=='\r') {p->quarantine=false;p->resync=0;return;}
        if(b==magic[p->resync]) p->resync++;
        else p->resync=(b==magic[0]);
        if(p->resync==6) {
            memcpy(p->frame,magic,6);p->used=6;p->resync=0;p->quarantine=false;
        }
        return;
    }
    if(p->trailing_newline) {
        if(b=='\r' || b=='\n') return;
        p->trailing_newline=false;
    }
    if(p->used<6) {
        if(b==magic[p->used]) {p->frame[p->used++]=b;return;}
        memcpy(p->text,p->frame,p->used);
        p->text_len=p->used;
        p->text[p->text_len++]=b;
        wipe(p);
        return;
    }
    p->frame[p->used++]=b;
    if(p->used>=9 && p->used==(size_t)p->frame[8]+10) {
        uint8_t sum=0;
        for(size_t i=0;i<p->used-1;i++) sum+=p->frame[i];
        bool valid=p->frame[6]==1 && p->frame[7]==3 && sum==b;
        cb(p->frame+9,p->frame[8],valid);
        wipe(p);
        /* Malformed lengths/checksums can leave credential bytes behind. */
        p->quarantine=!valid;
        p->resync=0;
        p->trailing_newline=valid;
    }
}
