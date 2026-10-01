"""Compile the real migrated sources with small host platform stubs.

Requires Python 3 and a C++17 compiler (CXX or g++).
Checks configurable log selection and fixed on-wire payload compatibility.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CXX = os.environ.get('CXX', 'g++')

STUBS = {
    'esp_err.h': '''#pragma once
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_ERR_INVALID_ARG=0x102;
''',
    'blackbox.h': '''#pragma once
#include <cstddef>
#include <cstdint>
#include "esp_err.h"
namespace Blackbox {
constexpr size_t TEXT_BUFFER_SIZE=200;
inline esp_err_t append_text(const char*, ...) { return ESP_OK; }
}
''',
    'freertos/FreeRTOS.h': '''#pragma once
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
''',
    'esp_log_write.h': '''#pragma once
#include <cstdarg>
using vprintf_like_t=int(*)(const char*, va_list);
inline vprintf_like_t test_log_hook=nullptr;
inline vprintf_like_t esp_log_set_vprintf(vprintf_like_t hook) {
    auto old=test_log_hook; test_log_hook=hook; return old;
}
''',
}

LOG_TEST = r'''
#include "blackbox_service.h"
#include "blackbox_service_internal.h"
#include "esp_log_write.h"
#include <cassert>
#include <cstring>
namespace BlackboxService::Internal {
esp_err_t start_worker() { return ESP_OK; }
esp_err_t sync_pending_logs() { return ESP_OK; }
uint32_t get_persist_failures() { return 0; }
}
void log_line(const char* fmt, ...) {
    va_list args; va_start(args,fmt); test_log_hook(fmt,args); va_end(args);
}
int main(int argc, char**) {
    using namespace BlackboxService;
    assert(init({nullptr,1})==ESP_ERR_INVALID_ARG);
    static const char* invalid[]={nullptr};
    assert(init({invalid,1})==ESP_ERR_INVALID_ARG);
    static const char* empty[]={""};
    assert(init({empty,1})==ESP_ERR_INVALID_ARG);
    static const char* tags[]={"Chosen", "Blackbox"};
    const bool configured=argc>1;
    assert(init(configured ? Config{tags,2} : Config{})==ESP_OK);
    // A second init must not silently change the installed capture policy.
    assert(init(configured ? Config{} : Config{tags,2})==ESP_OK);
    log_line("I (10) Chosen: selected\n");
    log_line("I (11) Other: noise\n");
    log_line("W (12) Other: warning\n");
    log_line("\x1b[31mE (13) Other: error\x1b[0m\n");
    log_line("E (14) Blackbox: recursive\n");
    log_line("I (15) Blackbox: recursive info\n");
    Internal::LogEvent event{};
    if (configured) {
        assert(Internal::pop_log_event(&event));
        assert(!strcmp(event.text,"[I][Chosen] selected"));
    }
    assert(Internal::pop_log_event(&event));
    assert(!strcmp(event.text,"[W][Other] warning"));
    assert(Internal::pop_log_event(&event));
    assert(!strcmp(event.text,"[E][Other] error"));
    assert(!Internal::pop_log_event(&event));
    Statistics stats{}; get_statistics(&stats);
    assert(stats.captured_logs==(configured ? 3u : 2u));
    assert(stats.dropped_logs==0 && stats.pending_logs==0);
}
'''

WIRE_TEST = r'''
#include "espnow_service_proto.h"
#include <cassert>
#include <cstring>
using namespace EspNowService;
using namespace EspNowService::Internal;
// Host stub: espnow_link's real implementation is not compiled in this test.
namespace EspNowLink {
bool MacAddress::is_broadcast() const {
    for (uint8_t value : bytes) {
        if (value != 0xFF) return false;
    }
    return true;
}
}
int main() {
    uint8_t out[40]{};
    const SwitchRequest request{0x12345678,SwitchAction::OFF};
    assert(encode_switch_request(request,out,4)==0);
    assert(encode_switch_request(request,nullptr,5)==0);
    assert(encode_switch_request(request,out,sizeof(out))==5);
    const uint8_t expected_request[]={0x78,0x56,0x34,0x12,0};
    assert(!memcmp(out,expected_request,5));
    const SwitchResponse response{0x12345678,SwitchAction::ON,SwitchResult::REJECTED,false};
    assert(encode_switch_response(response,out,sizeof(out))==7);
    const uint8_t expected_response[]={0x78,0x56,0x34,0x12,1,1,0};
    assert(!memcmp(out,expected_response,7));
    assert(encode_remote_battery(101,out,sizeof(out))==0);
    assert(encode_remote_battery(100,out,sizeof(out))==1 && out[0]==100);
    DataMessage data{}; data.request_id=1; data.available=true;
    data.data.status_flags=1; data.data.voltage_mv=12340;
    data.data.current_ua=-1000000; data.data.board_temperature_centi_c=-100;
    data.data.chip_temperature_centi_c=2500; data.data.charge_uah=-1;
    data.data.energy_uwh=2; data.data.meter_time_ms=3;
    assert(encode_data_message(data,out,39)==0);
    assert(encode_data_message(data,out,sizeof(out))==40);
    const uint8_t expected[]={
        1,0,0,0,1,1,0x34,0x30,0xc0,0xbd,0xf0,0xff,0x9c,0xff,0xc4,9,
        0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
        2,0,0,0,0,0,0,0,3,0,0,0,0,0,0,0};
    assert(!memcmp(out,expected,40));
}
'''

def main():
    with tempfile.TemporaryDirectory(prefix='wireless-shared-tests-') as tmp:
        work=Path(tmp)
        for name, text in STUBS.items():
            f=work/name; f.parent.mkdir(parents=True,exist_ok=True); f.write_text(text)
        log=ROOT/'components/middleware/blackbox_service'
        proto=ROOT/'components/product/espnow_service_proto'
        suites=[('log', LOG_TEST, [log/'src/blackbox_service.cpp',log/'src/blackbox_log_capture.cpp'],
                 [log/'include',log/'private_include']),
                ('wire',WIRE_TEST,[proto/'src/espnow_service_proto.cpp'],
                 [proto/'include',ROOT/'components/middleware/espnow_link/include'])]
        for name, source, sources, includes in suites:
            harness=work/f'{name}.cpp'; harness.write_text(source)
            binary=work/(name+('.exe' if os.name=='nt' else ''))
            command=[CXX,'-std=c++17','-Wall','-Wextra','-I'+str(work)]
            command+=['-I'+str(p) for p in includes]
            subprocess.run(command+[str(harness),*[str(s) for s in sources],'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
            if name=='log': subprocess.run([str(binary),'configured'],check=True)
            print(f'PASS {name}',flush=True)

if __name__=='__main__': main()
