"""Compile production progress methods with host clock/lock/output adapters.

The interpolation body comes from the pinned Sendspin library; the forwarding,
ownership check and display-state update bodies come from the firmware itself.
Requires Python and clang++ on PATH, or CXX=cl in a Visual Studio developer
shell. No device connection or flash.
"""
from pathlib import Path
import subprocess
import tempfile
import os

ROOT = Path(__file__).resolve().parents[2]


def body(path, signature):
    source = (ROOT / path).read_text(encoding='utf-8')
    start = source.index('{', source.index(signature))
    level = 1
    end = start + 1
    while level:
        level += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


poll = body('components/sendspin_sink/sendspin_sink.cpp', 'void present_progress()')
interpolate = body('components/sendspin_lib/src/metadata_role.cpp', 'uint32_t MetadataRole::Impl::get_track_progress_ms() const')
update = body('components/display/display.c', 'bool displayer_progress(')
owner = body('components/squeezelite/decode_external.c', 'static void sendspin_progress(')
metadata = body('components/sendspin_sink/sendspin_sink.cpp', 'void on_metadata(const')
clear = body('components/sendspin_sink/sendspin_sink.cpp', 'void on_metadata_clear()')
source = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include "sendspin/metadata_role.h"
using namespace sendspin;
static int64_t clock_us=1000000;
static int64_t platform_time_us(){return clock_us;}
constexpr int64_t US_PER_SECOND=1000000;
struct Client { int64_t now_us(){return clock_us;} int64_t get_client_time(int64_t t){return t;} } client;
struct Role { ServerMetadataStateObject metadata; Client *client=&::client;
 uint32_t get_track_progress_ms() const INTERPOLATE
} role;
enum { DISPLAYER_ACTIVE=2 };
static int display_object;
static int *display=&display_object;
static bool busy=false;
static int notifications=0,lock_depth=0;
#define pdTRUE 1
int xSemaphoreTake(int,int){return !busy;}
void xSemaphoreGive(int){}
void xTaskNotifyGive(int){++notifications;}
struct {bool active=false;} setup_screen;
struct {int mutex=1,state=DISPLAYER_ACTIVE,task=1; bool external_progress=false,live=false,timer=true; uint32_t elapsed=0;} displayer;
bool displayer_progress(bool known,uint32_t position_ms) UPDATE
constexpr int DECODE_SENDSPIN=4;
struct {int external=DECODE_SENDSPIN;} output;
uint32_t mn_sendspin_output_epoch=7;
#define LOCK_O ++lock_depth
#define UNLOCK_O --lock_depth
static void sendspin_progress(uint32_t epoch,bool known,uint32_t position_ms) OWNER
struct {void (*progress)(uint32_t,bool,uint32_t)=sendspin_progress;
 void metadata(uint32_t,const char*,const char*,const char*){}
} callbacks;
struct Listener {
 Client *client=&::client; Role *metadata_role=&role;
 std::atomic<bool> active{true}; std::atomic<uint32_t> epoch{7};
 ServerMetadataStateObject metadata; int64_t next_progress=0;
 decltype(callbacks)&output=callbacks;
 void present_progress() POLL
 void on_metadata(const ServerMetadataStateObject &m) METADATA
 void on_metadata_clear() CLEAR
} listener;
void set(std::optional<MetadataProgressObject> p){
 role.metadata={}; role.metadata.timestamp=clock_us; role.metadata.progress=p;
 listener.on_metadata(role.metadata);
}
void expect(bool live,uint32_t seconds){
 listener.present_progress(); assert(displayer.live==live && displayer.elapsed==seconds);
 assert(!displayer.timer && displayer.external_progress && !lock_depth);
}
int main(){
 set(MetadataProgressObject{10000,180000,1000}); expect(false,10);
 clock_us+=2250000; expect(false,12); // interpolation continues without new metadata
 int n=notifications; clock_us+=10000; expect(false,12); assert(notifications==n);
 set(MetadataProgressObject{12000,180000,0}); expect(false,12);
 clock_us+=5000000; expect(false,12); // pause freezes instead of running a local clock
 set(MetadataProgressObject{90000,180000,1000}); expect(false,90); // seek forwards
 set(MetadataProgressObject{3000,180000,1000}); expect(false,3); // seek backwards
 clock_us+=2000000; expect(false,5);
 set(MetadataProgressObject{0,240000,0}); expect(false,0); // paused at beginning is valid
 set(std::nullopt); expect(true,0); // radio without position
 set(MetadataProgressObject{0,0,0}); expect(true,0); // empty placeholder timeline
 set(MetadataProgressObject{0,0,1000}); expect(true,0); // MA radio has speed but no duration
 set(MetadataProgressObject{12000,0,1000}); expect(true,0); // real MA radio with elapsed position
 clock_us+=5000000; expect(true,0); // radio must remain LIVE as its server clock advances
 clock_us+=2000000; expect(true,0);
 listener.on_metadata_clear(); expect(true,0); // no stale clock after clear
 set(MetadataProgressObject{179000,180000,1000}); clock_us+=5000000; expect(false,180);
 listener.active=false; set(MetadataProgressObject{99000,180000,1000}); expect(false,180);
 listener.active=true; listener.epoch=6; listener.next_progress=0; expect(false,180); // stale owner
 listener.epoch=7; output.external=9; listener.next_progress=0; expect(false,180); // new source owns output
 output.external=DECODE_SENDSPIN; busy=true; listener.next_progress=0; expect(false,180);
 busy=false; clock_us+=300000; expect(false,99); // retry on next poll; no blocked audio lock
 setup_screen.active=true; set(std::nullopt); expect(false,99); // never overwrite Wi-Fi setup
 setup_screen.active=false; clock_us+=300000; expect(true,0);
 displayer.state=0; set(MetadataProgressObject{4000,10000,1000}); expect(true,0);
 puts("PASS: play, pause, seek, live, clear, end clamp, poll cadence, source ownership and nonblocking display retry");
}
'''
for token, value in [('INTERPOLATE', interpolate), ('UPDATE', update), ('OWNER', owner),
                     ('POLL', poll), ('METADATA', metadata), ('CLEAR', clear)]:
    source = source.replace(token, value)
with tempfile.TemporaryDirectory() as folder:
    folder = Path(folder)
    cpp, exe = folder / 'progress.cpp', folder / 'progress.exe'
    cpp.write_text(source, encoding='utf-8')
    compiler = os.environ.get('CXX', 'clang++')
    include = str(ROOT/'components/sendspin_lib/include')
    if Path(compiler).stem.lower() == 'cl':
        args = [compiler, '/nologo', '/EHsc', '/std:c++17', '/I'+include,
                str(cpp), '/Fe:'+str(exe), '/Fo:'+str(folder/'progress.obj')]
    else:
        args = [compiler, '-std=c++17', '-I'+include, str(cpp), '-o', str(exe)]
    subprocess.run(args, check=True)
    subprocess.run([str(exe)], check=True)
