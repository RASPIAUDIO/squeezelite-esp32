"""Render with production GDS font/draw code; independently decode QR with OpenCV.
Requires clang and opencv-python. Outputs are synthetic, never real credentials.
"""
import argparse, subprocess, tempfile
from pathlib import Path
import cv2

ROOT=Path(__file__).resolve().parents[2]
SRC=ROOT/'components/display'
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args()
a.out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory() as tmp:
    t=Path(tmp)
    (t/'esp_attr.h').write_text('#define IRAM_ATTR\n')
    (t/'esp_log.h').write_text('#define ESP_LOGE(...) ((void)0)\n#define ESP_LOGI(...) ((void)0)\n')
    (t/'esp_heap_caps.h').write_text('#include <stdlib.h>\n#define MALLOC_CAP_INTERNAL 0\n#define MALLOC_CAP_DMA 0\n#define heap_caps_malloc(n,c) malloc(n)\n')
    exe=t/'wifi-test.exe'
    subprocess.run(['clang','-funsigned-char','-D_CRT_SECURE_NO_WARNINGS',
        '-I'+str(t),'-I'+str(SRC),'-I'+str(SRC/'core'),
        str(Path(__file__).with_name('wifi_setup_host.c')),
        *[str(SRC/s) for s in ['wifi_setup.c','core/gds_font.c','core/gds_draw.c',
          'qr/qrcodegen.c','fonts/font_droid_sans_mono_7x13.c']],'-o',str(exe)],check=True)
    cases=[('normal','squeezelite-d01844','squeezelite',False),
           ('escaped','A;B:C,D"E\\F','pass;,:"\\word',False),
           ('maximum',';'*32,';'*64,False),('open','MnCast Open','',True)]
    def esc(s):return ''.join(('\\' if c in '\\;,:"' else '')+c for c in s)
    for w,h in [(320,240),(480,320)]:
        for name,ssid,password,op in cases:
            path=a.out/f'{name}-{w}.ppm'
            subprocess.run([str(exe),ssid,password,str(int(op)),str(path),str(w),str(h)],check=True)
            im=cv2.imread(str(path))
            crop=cv2.resize(im[32:h-60,w//2:w-8],None,fx=3,fy=3,interpolation=cv2.INTER_NEAREST)
            data,_,_=cv2.QRCodeDetector().detectAndDecode(crop)
            expected=f'WIFI:T:{"nopass" if op else "WPA"};S:{esc(ssid)};P:{"" if op else esc(password)};;'
            assert data==expected,(name,w,data)
            # Top finder border is solid, never stripes separated by missing columns.
            qr=im[34:h-80,w//2:w-12]
            white=(qr[:,:,0]==255)&(qr[:,:,1]==255)&(qr[:,:,2]==255)
            ys,xs=white.nonzero();block=qr[ys.min():ys.max()+1,xs.min():xs.max()+1]
            black=(block==0).all(axis=2)
            top=black.nonzero()[0].min();line=black[top]
            longest=max(len(run) for run in ''.join('1' if v else '0' for v in line).split('0'))
            assert longest>=14, 'QR finder has missing columns'
            # The margin stays clear even with the longest legal credentials.
            assert not im[34:,:10].any() and not im[34:,-10:].any()
            assert not im[-10:].any()
            cv2.imwrite(str(path.with_suffix('.png')),im)
            print(f'{name} {w}x{h}: QR, solid modules, margins, GDS state and ILI9341 transfers OK')
