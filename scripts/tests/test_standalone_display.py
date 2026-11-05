"""Host checks using the actual renderer, PCM collector and spectrum source.
Requires clang; optionally Pillow for PNG previews. No device credentials.
"""
import argparse, shutil, subprocess, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'components/display'
p = argparse.ArgumentParser(); p.add_argument('--out', type=Path, required=True)
a = p.parse_args(); a.out.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory() as tmp:
    t = Path(tmp)
    (t/'esp_attr.h').write_text('#define IRAM_ATTR\n')
    (t/'esp_log.h').write_text('#define ESP_LOGE(...) ((void)0)\n#define ESP_LOGI(...) ((void)0)\n')
    (t/'esp_heap_caps.h').write_text('#include <stdlib.h>\n#define MALLOC_CAP_INTERNAL 0\n#define MALLOC_CAP_DMA 0\n#define heap_caps_malloc(n,c) malloc(n)\n')
    # Reuse the panel emulator: real GDS drawing AND real ILI9341 SPI writes.
    host = Path(__file__).with_name('wifi_setup_host.c').read_text()
    host = host.replace('#include "wifi_setup.h"', '#include "now_playing.h"')
    start = host.index('    assert(wifi_setup_render')
    end = host.index('    FILE*f=', start)
    host = host[:start] + r'''
    assert(now_playing_supported(&d));
    uint16_t art[80*80];
    for(int y=0;y<80;y++)for(int x=0;x<80;x++)art[y*80+x]=0x0108+((x/4)<<11)+((y/4)<<5);
    GDS_Clear(&d,0);
    now_playing_cover(&d,art,80,80);
    now_playing_title(&d,"Blue Hours - North Coast",0);
    now_playing_info(&d,"AIRPLAY",154);
    uint8_t levels[12]={12,32,48,85,65,38,24,32,44,55,36,18},peaks[12]={0};
    now_playing_spectrum(&d,levels,peaks);GDS_Update(&d);
    assert(d.TextWidth==99 && d.FontForceProportional && !d.FontForceMonospace);
    unsigned short saved[480*320];memcpy(saved,pixels,d.FramebufferSize);
    int side=now_playing_cover_size(&d),cx=d.Width-12-side;
    // Scrolling, long source names and spectrum clearing cannot overwrite artwork.
    now_playing_title(&d,"Long artist and title scrolling across the entire display",130);
    now_playing_info(&d,"VERY LONG SOURCE NAME",99999);
    memset(peaks,0,sizeof(peaks));now_playing_spectrum(&d,NULL,peaks);GDS_Update(&d);
    for(int y=28;y<28+side;y++)for(int x=cx;x<cx+side;x++)assert(pixels[y*d.Width+x]==saved[y*d.Width+x]);
    for(int y=36;y<=d.Height-72;y++)for(int x=8;x<cx-12;x++)assert(pixels[y*d.Width+x]==0);
    memcpy(pixels,saved,d.FramebufferSize);GDS_Update(&d);
    // Repeated animation and timer changes: emulate every real panel transfer.
    for (unsigned frame=0;frame<400;frame++) {
        for(int i=0;i<12;i++)levels[i]=(frame*17+i*23)%101;
        now_playing_spectrum(&d,levels,peaks);
        now_playing_info(&d,"SENDSPIN",frame,false);
        GDS_Update(&d);
    }
    memset(peaks,0,sizeof(peaks));now_playing_spectrum(&d,NULL,peaks);
    now_playing_info(&d,"SENDSPIN",0,true);GDS_Update(&d);
    unsigned short final[480*320];memcpy(final,pixels,d.FramebufferSize);
    // An animated region after erasing must match the same scene drawn fresh.
    GDS_Clear(&d,0);now_playing_info(&d,"SENDSPIN",0,true);
    memset(peaks,0,sizeof(peaks));now_playing_spectrum(&d,NULL,peaks);GDS_Update(&d);
    for(int y=36;y<d.Height-4;y++)for(int x=8;x<cx-8;x++)
        assert(pixels[y*d.Width+x]==final[y*d.Width+x]);
    memcpy(pixels,saved,d.FramebufferSize);GDS_Update(&d);
    // Corrupt both margins of a panel row, then change its central pixel.
    // A complete stripe must restore the margins from the clean framebuffer.
    for(int fallback=0;fallback<2;fallback++) {
        if(fallback){free(priv->iRAM);priv->iRAM=NULL;}
        int row=d.Height-50;
        panel[(row*d.Width)*2]^=0xff;
        panel[(row*d.Width+d.Width-1)*2]^=0xff;
        pixels[row*d.Width+20]^=0xffff;
        GDS_Update(&d);
        // Failed transfers must not advance the shadow: a later update retries.
        pixels[row*d.Width+20]^=0xffff;
        fail_payload=true;Update16(&d);
        assert(memcmp(panel,pixels,d.FramebufferSize));
        GDS_Update(&d);
    }
    memcpy(pixels,saved,d.FramebufferSize);GDS_Update(&d);
''' + host[end:]
    (t/'display_host.c').write_text(host)
    exe=t/'display.exe'
    subprocess.run(['clang','-funsigned-char','-D_CRT_SECURE_NO_WARNINGS','-I'+str(t),'-I'+str(SRC),'-I'+str(SRC/'core'),str(t/'display_host.c'),*[str(SRC/s) for s in ['now_playing.c','core/gds_font.c','core/gds_draw.c','fonts/font_droid_sans_mono_7x13.c','fonts/font_liberation_mono_9x15.c']],'-o',str(exe)],check=True)
    for w,h in [(320,240),(480,320)]:
        ppm=a.out/f'now-playing-{w}.ppm'
        subprocess.run([str(exe),'','','',str(ppm),str(w),str(h)],check=True)
        from PIL import Image
        Image.open(ppm).save(ppm.with_suffix('.png'))
        print(f'{w}x{h}: artwork isolation, margins, spectrum disable, font state and panel transfers OK')

    # Compile unmodified production PCM/FFT modules against a host DFT backend.
    for name in ['output_visu.c','standalone_spectrum.c']:
        shutil.copyfile(ROOT/'components/squeezelite'/name,t/name)
    (t/'squeezelite.h').write_text(r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
typedef int16_t s16_t;typedef uint8_t u8_t;typedef uint32_t u32_t;typedef unsigned frames_t;
typedef int log_level;
#define lINFO 0
#define EXT_BSS
#define EXT_RAM_BSS_ATTR
#define LOG_INFO(...) ((void)0)
#define min(a,b) ((a)<(b)?(a):(b))
typedef int pthread_mutex_t;
static inline int pthread_mutex_init(int*m,void*x){*m=0;return 0;}
static inline int pthread_mutex_trylock(int*m){if(*m)return 1;*m=1;return 0;}
static inline int pthread_mutex_lock(int*m){*m=1;return 0;}
static inline int pthread_mutex_unlock(int*m){*m=0;return 0;}
struct visu_export_s{int mutex;unsigned level,size,rate,gain;void*buffer;bool running;};
extern struct visu_export_s visu_export;
''')
    (t/'display.h').write_text('#include <stdbool.h>\n#include <stdint.h>\nextern bool (*display_spectrum)(uint8_t*,unsigned);\n')
    (t/'esp_timer.h').write_text('#include <stdint.h>\nextern int64_t clock_us;\nstatic inline int64_t esp_timer_get_time(void){return clock_us;}\n')
    (t/'esp_dsp.h').write_text(r'''
#include <math.h>
static inline void dsps_wind_hann_f32(float*w,int n){for(int i=0;i<n;i++)w[i]=.5f*(1-cosf(6.28318530718f*i/(n-1)));}
static inline void dsps_fft2r_fc32(float*s,int n){float out[256];for(int k=0;k<n;k++){double r=0,v=0;for(int j=0;j<n;j++){double t=-6.28318530718*k*j/n;r+=s[2*j]*cos(t);v+=s[2*j]*sin(t);}out[2*k]=r;out[2*k+1]=v;}for(int i=0;i<2*n;i++)s[i]=out[i];}
static inline void dsps_bit_rev_fc32_ansi(float*s,int n){}
''')
    (t/'pcm_host.c').write_text(r'''
#include "squeezelite.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
bool (*display_spectrum)(uint8_t*,unsigned);
int64_t clock_us=1000000;
void output_visu_init(log_level);void output_visu_export(void*,frames_t,u32_t,bool,u32_t);
void standalone_spectrum_init(void);
int main(void){
 output_visu_init(0);standalone_spectrum_init();
 unsigned rates[]={44100,48000,96000,192000};uint8_t levels[12];
 for(unsigned r=0;r<4;r++){
  unsigned rate=rates[r];unsigned char frames[512*BYTES_PER_FRAME];
  for(int i=0;i<512;i++)for(int ch=0;ch<2;ch++){
   int16_t value=(int16_t)(16000*sin(6.28318530718*3000*i/rate)*(ch?-1:1));
   if(BYTES_PER_FRAME==8){int32_t v=(int32_t)value*65536;memcpy(frames+i*8+ch*4,&v,4);}
   else memcpy(frames+i*4+ch*2,&value,2);
  }
  // Fragmented writes caught the original frame-offset/byte-offset corruption.
  output_visu_export(frames,47,rate,false,65536);
  output_visu_export(frames+47*BYTES_PER_FRAME,465,rate,false,65536);
  assert(visu_export.level==512 && !memcmp(frames,visu_export.buffer,sizeof(frames)));
  assert(display_spectrum(levels,12));unsigned peak=0,index=0;
  for(int j=0;j<12;j++)if(levels[j]>peak){peak=levels[j];index=j;}
  assert(peak>60 && index>=5 && index<=8); // 3kHz stays visible with inverted stereo.
  clock_us+=400000;display_spectrum(levels,12);for(int j=0;j<12;j++)assert(!levels[j]);
  output_visu_export(frames,512,rate,false,65536);
  output_visu_export(frames,1,rate,true,65536);assert(!visu_export.running && !visu_export.level);
  printf("PCM%d %u: fragmented writes, opposite-phase tone, stale/silence decay OK\n",BYTES_PER_FRAME*4,rate);
 }
 return 0;
}
''')
    for depth in [4,8]:
        exe=t/f'pcm{depth}.exe'
        subprocess.run(['clang',f'-DBYTES_PER_FRAME={depth}','-I'+str(t),str(t/'pcm_host.c'),str(t/'output_visu.c'),str(t/'standalone_spectrum.c'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
