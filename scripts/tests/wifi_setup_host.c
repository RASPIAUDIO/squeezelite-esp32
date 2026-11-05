/* Host framebuffer backend. Font and drawing primitives are the REAL GDS code. */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "gds_private.h"
#include "gds_font.h"
#include "wifi_setup.h"
#define strcasestr strstr
#include "ILI9341.c"
static unsigned short pixels[480*320];
static unsigned char panel[480*320*2];
static int command,x0,x1,y0,y1,cursor;
static bool fail_payload;
static bool panel_command(struct GDS_Device*d,uint8_t c) {
    (void)d;command=c;if(c==0x2c)cursor=0;return true;
}
static bool panel_data(struct GDS_Device*d,const uint8_t *data,size_t n) {
    if(command==0x2a || command==0x2b) {
        assert(n==4);int lo=data[0]*256+data[1],hi=data[2]*256+data[3];
        if(command==0x2a){x0=lo;x1=hi;}else{y0=lo;y1=hi;}
    }else if(command==0x2c){
        if(fail_payload){fail_payload=false;return false;}
        // Every RGB565 payload must fill a newly addressed window in one burst.
        assert(cursor==0 && n==(size_t)(x1-x0+1)*(y1-y0+1)*2);
        assert(x0==0 && x1==d->Width-1);
        for(size_t i=0;i<n;i++,cursor++){
            int y=y0+cursor/((x1-x0+1)*2),x=x0+(cursor/2)%(x1-x0+1);
            assert(y<=y1 && x<d->Width && y<d->Height);
            panel[(y*d->Width+x)*2+(cursor%2)]=data[i];
        }
    }return true;
}
static void pixel(struct GDS_Device *d,int x,int y,int c) {
    assert(x>=0 && x<d->Width && y>=0 && y<d->Height);
    pixels[y*d->Width+x]=c;
}
int GDS_GetWidth(struct GDS_Device*d){return d->Width;}
int GDS_GetHeight(struct GDS_Device*d){return d->Height;}
int GDS_GetMode(struct GDS_Device*d){return d->Mode;}
void GDS_SetTextWidth(struct GDS_Device*d,int w){d->TextWidth=w?w:d->Width;}
void GDS_Clear(struct GDS_Device*d,int c){for(int i=0;i<d->Width*d->Height;i++)pixels[i]=c;}
void GDS_SetContrast(struct GDS_Device*d,uint8_t c){(void)d;(void)c;}
void GDS_Update(struct GDS_Device*d){
    Update16(d);
    assert(!memcmp(panel,pixels,d->FramebufferSize));
}
int main(int argc,char**argv) {
    assert(argc==7);
    struct GDS_Device d={.Width=atoi(argv[5]),.Height=atoi(argv[6]),.TextWidth=99,
        .Depth=16,.Mode=GDS_RGB565,.Font=&Font_droid_sans_mono_7x13,
        .FontForceProportional=true,.DrawPixelFast=pixel,
        .WriteCommand=panel_command,.WriteData=panel_data};
    d.Framebuffer=(uint8_t*)pixels;d.FramebufferSize=d.Width*d.Height*2;
    struct PrivateSpace *priv=(struct PrivateSpace*)d.Private;
    priv->Shadowbuffer=malloc(d.FramebufferSize);priv->iRAM=malloc(PAGE_BLOCK);
    memset(priv->Shadowbuffer,0xaa,d.FramebufferSize);
    assert(wifi_setup_render(&d,"Previous screen","previous-password","192.168.4.1",false));
    assert(wifi_setup_render(&d,argv[1],argv[2],"192.168.4.1",atoi(argv[3])));
    assert(d.TextWidth==99 && d.FontForceProportional && !d.FontForceMonospace);
    GDS_Update(&d); /* An unchanged frame must retain every pixel too. */
    FILE*f=fopen(argv[4],"wb");assert(f);
    fprintf(f,"P6\n%d %d\n255\n",d.Width,d.Height);
    for(int i=0;i<d.Width*d.Height;i++) {
        unsigned c=pixels[i];unsigned char rgb[]={((c>>11)&31)*255/31,((c>>5)&63)*255/63,(c&31)*255/31};
        fwrite(rgb,1,3,f);
    }
    fclose(f);return 0;
}
