"""Run the actual pairing/peer-store and link cancellation sources with host platform stubs."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
LINK = ROOT / 'components/middleware/espnow_link'
CXX = os.environ.get('CXX', 'g++')
STUBS = {
    'sdkconfig.h': '#pragma once\n#define CONFIG_ESPNOW_LINK_TASK_STACK_SIZE 4096\n',
    'esp_attr.h': '#pragma once\n#define IRAM_ATTR\n',
    'esp_err.h': '''#pragma once
using esp_err_t=int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_NO_MEM=0x101, ESP_ERR_INVALID_ARG=0x102,
ESP_ERR_INVALID_STATE=0x103, ESP_ERR_NOT_FOUND=0x105, ESP_ERR_TIMEOUT=0x107,
ESP_ERR_INVALID_RESPONSE=0x108, ESP_ERR_ESPNOW_EXIST=0x306b;
inline const char* esp_err_to_name(int) { return "test"; }
''',
    'esp_log.h': '''#pragma once
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
''',
    'freertos/FreeRTOS.h': '''#pragma once
#include <cstdint>
using TickType_t=uint32_t;
using UBaseType_t=unsigned;
using portMUX_TYPE=int;
inline TickType_t test_tick=100;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define portENTER_CRITICAL_ISR(x) ((void)(x))
#define portEXIT_CRITICAL_ISR(x) ((void)(x))
#define pdMS_TO_TICKS(x) (x)
#define portTICK_PERIOD_MS 1
#define portMAX_DELAY 0xffffffffu
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
''',
    'freertos/queue.h': '''#pragma once
#include "FreeRTOS.h"
#include <deque>
#include <vector>
#include <cstring>
struct TestQueue { size_t capacity, size; std::deque<std::vector<uint8_t>> data; };
using QueueHandle_t=TestQueue*;
inline QueueHandle_t xQueueCreate(size_t cap,size_t size) { return new TestQueue{cap,size,{}}; }
inline int xQueueSend(QueueHandle_t q,const void* value,TickType_t) {
 if (!q || q->data.size()==q->capacity) return 0;
 const auto* p=static_cast<const uint8_t*>(value); q->data.emplace_back(p,p+q->size); return 1;
}
inline int xQueueReceive(QueueHandle_t q,void* value,TickType_t wait) {
 if (!q || q->data.empty()) { test_tick+=wait; return 0; }
 memcpy(value,q->data.front().data(),q->size); q->data.pop_front(); return 1;
}
inline void xQueueReset(QueueHandle_t q) { q->data.clear(); }
inline size_t uxQueueMessagesWaiting(QueueHandle_t q) { return q->data.size(); }
inline void vQueueDelete(QueueHandle_t q) { delete q; }
''',
    'freertos/task.h': '''#pragma once
#include "FreeRTOS.h"
using TaskHandle_t=void*;
inline TickType_t xTaskGetTickCount() { return test_tick; }
inline void vTaskDelay(TickType_t n) { test_tick+=n; }
inline int xTaskCreate(void(*)(void*),const char*,uint32_t,void*,unsigned,TaskHandle_t* out) {
 if (out) { *out=reinterpret_cast<void*>(1); } return pdPASS;
}
inline void vTaskDelete(TaskHandle_t) {}
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return reinterpret_cast<void*>(2); }
''',
    'freertos/semphr.h': '''#pragma once
#include "FreeRTOS.h"
using SemaphoreHandle_t=void*;
inline int xSemaphoreTakeRecursive(SemaphoreHandle_t,TickType_t) { return 1; }
inline int xSemaphoreGiveRecursive(SemaphoreHandle_t) { return 1; }
''',
    'esp_timer.h': '#pragma once\n#include "freertos/FreeRTOS.h"\ninline int64_t esp_timer_get_time() { return int64_t(test_tick)*1000; }\n',
    'esp_random.h': '''#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
inline uint32_t esp_random() { static uint32_t n=42; return ++n; }
inline void esp_fill_random(void* p,size_t n) { memset(p,0x55,n); }
''',
    'esp_wifi.h': '''#pragma once
#include <cstdint>
enum wifi_second_chan_t { WIFI_SECOND_CHAN_NONE=0 };
constexpr int WIFI_IF_STA=0, WIFI_OFFCHAN_TX_REQ=1;
struct wifi_country_t { uint8_t schan=1,nchan=13; };
''',
    'esp_now.h': '''#pragma once
#include "esp_err.h"
#include "esp_wifi.h"
#include <cstddef>
struct esp_now_peer_info_t { uint8_t peer_addr[6]{},lmk[16]{}; uint8_t channel{}; int ifidx{}; bool encrypt{}; };
struct esp_now_switch_channel_t {
 int type{}; uint8_t channel{}; wifi_second_chan_t sec_channel{}; uint16_t wait_time_ms{};
 uint8_t op_id{},dest_mac[6]{}; uint16_t data_len{}; uint8_t data[0];
};
inline int driver_sends=0;
inline int esp_now_send(const uint8_t*,const uint8_t*,size_t) { ++driver_sends; return ESP_OK; }
inline int esp_now_switch_channel_tx(esp_now_switch_channel_t*) { return ESP_FAIL; }
inline bool esp_now_is_peer_exist(const uint8_t*) { return true; }
inline int esp_now_mod_peer(esp_now_peer_info_t*) { return ESP_OK; }
inline int esp_now_add_peer(esp_now_peer_info_t*) { return ESP_OK; }
inline int esp_now_del_peer(const uint8_t*) { return ESP_OK; }
''',
    'wifi_manager.h': '''#pragma once
#include "esp_now.h"
struct MAC_t { uint8_t bytes[6]{1,2,3,4,5,6}; };
class WiFiManager {
 uint8_t channel_=1;
public:
 static WiFiManager& instance() { static WiFiManager w; return w; }
 MAC_t get_mac(int) { return {}; }
 int set_channel(uint8_t channel) { channel_=channel; return ESP_OK; }
 int get_channel(uint8_t* c,wifi_second_chan_t*) { *c=channel_; return ESP_OK; }
 int get_country(wifi_country_t* country) { *country={}; return ESP_OK; }
};
''',
    'HXC_NVS.h': '''#pragma once
#include "esp_err.h"
inline bool fail_next_store=false;
namespace HXC {
template<class T> class NVS_DATA {
 T value_;
public:
 NVS_DATA(const char*,T value):value_(value) {}
 T read() { return value_; }
 esp_err_t set(const T& value) {
  if (fail_next_store) { fail_next_store=false; return ESP_FAIL; }
  value_=value; return ESP_OK;
 }
};
}
''',
}

PAIRING = r'''
#include <cassert>
#include <vector>
#include "espnow_link_internal.h"
#include "espnow_protocol.h"
namespace EspNowLink::Internal {
std::atomic_bool initialized{true},active{true};
SemaphoreHandle_t lifecycle_mutex=nullptr;
TaskHandle_t task_handle=nullptr;
}
struct Sent { EspNowLink::MacAddress peer; uint16_t id; std::vector<uint8_t> payload;
 EspNowLink::SendCallback cb; void* context; };
std::vector<Sent> sent;
std::vector<EspNowLink::PeerConfig> installed;
namespace EspNowLink {
const MacAddress BROADCAST_ADDRESS={{255,255,255,255,255,255}};
bool MacAddress::operator==(const MacAddress& other) const { return !memcmp(bytes,other.bytes,6); }
bool MacAddress::operator!=(const MacAddress& other) const { return !(*this==other); }
bool MacAddress::is_broadcast() const { return *this==BROADCAST_ADDRESS; }
bool is_active() { return Internal::active.load(); }
esp_err_t add_peer(const PeerConfig& peer) {
 for (auto& old:installed) if (old.address==peer.address) { old=peer; return ESP_OK; }
 installed.push_back(peer); return ESP_OK;
}
esp_err_t remove_peer(const MacAddress& peer) {
 for (auto i=installed.begin();i!=installed.end();++i) if(i->address==peer) { installed.erase(i); return ESP_OK; }
 return ESP_ERR_NOT_FOUND;
}
bool has_peer(const MacAddress& peer) { for(auto& p:installed) if(p.address==peer) return true; return false; }
esp_err_t send(const MacAddress& peer,uint16_t id,const void* p,size_t n,const SendOptions&,SendCallback cb,void* context) {
 auto* bytes=static_cast<const uint8_t*>(p); sent.push_back({peer,id,{bytes,bytes+n},cb,context}); return ESP_OK;
}
esp_err_t register_handler(uint16_t,MessageHandler,void*) { return ESP_OK; }
esp_err_t unregister_handler(uint16_t) { return ESP_OK; }
uint16_t get_channel_probe_timeout_ms(const MacAddress&) { return 20; }
}
namespace EspNowLink::Internal {
esp_err_t encode_frame(uint8_t,uint16_t,uint32_t,uint32_t,const uint8_t*,size_t,uint8_t*,size_t,size_t*) { return ESP_OK; }
}
#include "espnow_pairing.cpp"
#include "espnow_peer_store.cpp"
#include "espnow_pairing_protocol.cpp"
using namespace EspNowLink;
using namespace EspNowLink::Internal;
MacAddress old_peer={{7,7,7,7,7,7}}, new_peer={{8,8,8,8,8,8}}, other={{9,9,9,9,9,9}};
void pump() { Event e{}; while(xQueueReceive(events,&e,0)==pdTRUE) process_event(e); }
void complete_send(uint16_t id,SendResult result) {
 for(auto i=sent.rbegin();i!=sent.rend();++i) if(i->id==id && i->cb) {
  auto cb=i->cb; auto ctx=i->context; cb(result,1,ctx); pump(); return;
 }
 assert(false);
}
void deliver(uint16_t id,const MacAddress& peer,uint32_t nonce,bool reliable=true,const uint8_t* key=nullptr,esp_err_t error=ESP_OK) {
 uint8_t payload[24]{}; size_t size=encode_nonce(nonce,payload,sizeof(payload));
 if(id==MSG_PAIR_REQUEST_V2) { payload[4]=2; size=5; }
 if(id==MSG_PAIR_RESPONSE_V2 || id==MSG_PAIR_RESPONSE) size=encode_pair_response(nonce,key,payload,sizeof(payload));
 if(id==MSG_PAIR_RESULT) { Codec::store_le<uint32_t>(payload+4,error); size=8; }
 Message m{}; m.source=peer; m.destination=local_mac(); m.payload=payload; m.payload_size=size;
 m.message_id=id; m.channel=1; m.reliable=reliable; on_message(m,nullptr); pump();
}
void responder() {
 assert(enter_pairing_mode(60000)==ESP_OK); pump(); assert(phase==Phase::LISTENING);
}
void initiator_ready() {
 assert(start_pairing()==ESP_OK); pump();
 Event info{}; info.source=new_peer; info.channel=transaction.channel; info.nonce=transaction.nonce;
 info.message_id=MSG_DISCOVERY_INFO; info.role=1; process_event(info);
 test_tick+=DISCOVERY_WAIT_MS; tick(); assert(phase==Phase::RESPONSE);
 uint8_t key[KEY_SIZE]; memset(key,0x66,sizeof(key));
 deliver(MSG_PAIR_RESPONSE_V2,new_peer,transaction.nonce,true,key); assert(phase==Phase::CONFIRM);
}
int main() {
 assert(init_pairing()==ESP_OK);
 PeerConfig original{}; original.address=old_peer; original.channel=1; original.encrypted=true;
 memset(original.lmk,0x44,KEY_SIZE);
 assert(save_peer(original,1,1)==ESP_OK); assert(add_peer(original)==ESP_OK);
 // Single-binding migration is atomic and preserves the old LMK.
 PeerConfig extra=original; extra.address=other; save_peer(extra,1,1);
 PairingConfig policy{}; policy.local_role=2; policy.accepted_roles=1u<<1;
 policy.peer_limit=1; policy.replace_existing=true;
 assert(configure_pairing(policy)==ESP_OK); assert(get_saved_peer_count()==1 && !has_peer(other));
 policy.peer_limit=3; policy.replace_existing=false; policy.local_role=1; policy.accepted_roles=1u<<2;
 assert(configure_pairing(policy)==ESP_OK);
 // Dropped encrypted confirmation releases the responder transaction independently of the window.
 responder(); deliver(MSG_PAIR_REQUEST_V2,old_peer,123,false);
 complete_send(MSG_PAIR_RESPONSE_V2,SendResult::SENT); assert(phase==Phase::WAIT_CONFIRM);
 assert(transaction.lmk[0]==0x44);
 test_tick+=TRANSACTION_WAIT_MS; tick(); assert(phase==Phase::LISTENING && has_peer(old_peer));
 // Retrying the same device succeeds, retaining one NVS slot.
 deliver(MSG_PAIR_REQUEST_V2,old_peer,124,false); complete_send(MSG_PAIR_RESPONSE_V2,SendResult::SENT);
 deliver(MSG_PAIR_CONFIRM,old_peer,124); assert(get_saved_peer_count()==1);
 complete_send(MSG_PAIR_RESULT,SendResult::ACKNOWLEDGED); assert(!is_pairing());
 // A committed responder must not report delivery success when the result ACK is lost.
 responder(); deliver(MSG_PAIR_REQUEST_V2,old_peer,126,false); complete_send(MSG_PAIR_RESPONSE_V2,SendResult::SENT);
 deliver(MSG_PAIR_CONFIRM,old_peer,126); complete_send(MSG_PAIR_RESULT,SendResult::NO_ACK);
 PairingResult lost_result{}; get_pairing_result(&lost_result);
 assert(lost_result.error==ESP_ERR_TIMEOUT && get_saved_peer_count()==1);
 // Full capacity rejects a new MAC without installing a temporary peer; wrong roles are ignored.
 policy.peer_limit=1; assert(configure_pairing(policy)==ESP_OK); responder();
 deliver(MSG_PAIR_REQUEST_V2,new_peer,127,false);
 assert(phase==Phase::LISTENING && !has_peer(new_peer) && get_saved_peer_count()==1);
 Event wrong_role{}; wrong_role.message_id=MSG_PAIR_REQUEST_V2; wrong_role.source=old_peer;
 wrong_role.nonce=128; wrong_role.channel=1; wrong_role.role=1; process_event(wrong_role);
 assert(phase==Phase::LISTENING);
 // Legacy payloads remain interoperable and results expose their weaker confirmation path.
 deliver(MSG_PAIR_REQUEST,old_peer,129,false); complete_send(MSG_PAIR_RESPONSE,SendResult::SENT);
 deliver(MSG_PAIR_CONFIRM,old_peer,129); get_pairing_result(&lost_result);
 assert(!is_pairing() && lost_result.error==ESP_OK && lost_result.legacy);
 policy.peer_limit=3; assert(configure_pairing(policy)==ESP_OK);
 // Persistence failure is reported, not a successful ACK-only pairing.
 responder(); deliver(MSG_PAIR_REQUEST_V2,new_peer,125,false); complete_send(MSG_PAIR_RESPONSE_V2,SendResult::SENT);
 fail_next_store=true; deliver(MSG_PAIR_CONFIRM,new_peer,125);
 assert(!transaction.committed && get_saved_peer_count()==1);
 complete_send(MSG_PAIR_RESULT,SendResult::ACKNOWLEDGED);
 PairingResult result{}; get_pairing_result(&result); assert(result.error==ESP_FAIL);
 // Initiator ignores wrong sources and does not save before the explicit V2 result.
 policy.local_role=2; policy.accepted_roles=1u<<1; policy.peer_limit=1; policy.replace_existing=true;
 assert(configure_pairing(policy)==ESP_OK); initiator_ready();
 complete_send(MSG_PAIR_CONFIRM,SendResult::ACKNOWLEDGED); assert(phase==Phase::RESULT);
 assert(load_store().peers[0].mac[0]==7);
 deliver(MSG_PAIR_RESULT,other,transaction.nonce); assert(phase==Phase::RESULT);
 fail_next_store=true; deliver(MSG_PAIR_RESULT,new_peer,transaction.nonce);
 assert(load_store().peers[0].mac[0]==7 && has_peer(old_peer));
 get_pairing_result(&result); assert(result.error==ESP_FAIL);
 // Successful replacement leaves exactly one saved/runtime target.
 initiator_ready(); deliver(MSG_PAIR_RESULT,new_peer,transaction.nonce);
 assert(!is_pairing() && get_saved_peer_count()==1 && load_store().peers[0].mac[0]==8 && !has_peer(old_peer));
 // A delayed send completion cannot complete a new transaction.
 complete_send(MSG_PAIR_CONFIRM,SendResult::ACKNOWLEDGED); assert(!is_pairing());
 // Two compatible candidates are rejected instead of choosing the first response.
 assert(start_pairing()==ESP_OK); pump();
 Event info{}; info.channel=transaction.channel; info.nonce=transaction.nonce; info.message_id=MSG_DISCOVERY_INFO; info.role=1;
 info.source=old_peer; process_event(info); info.source=other; process_event(info);
 test_tick+=DISCOVERY_WAIT_MS; tick(); get_pairing_result(&result);
 assert(!is_pairing() && result.error==ESP_ERR_INVALID_RESPONSE && load_store().peers[0].mac[0]==8);
 // Callback-triggered cancellation is nonblocking and prevents a late success.
 assert(start_pairing()==ESP_OK); pump();
 Internal::task_handle=xTaskGetCurrentTaskHandle(); leave_pairing_mode(); assert(cancel_requested.load());
 Internal::task_handle=nullptr;
 finish(ESP_ERR_INVALID_STATE); cancel_requested.store(false); xQueueReset(events);
 deinit_pairing();
}
'''

LINK_TEST = r'''
#include <cassert>
#include "espnow_link_internal.h"
#include "espnow_protocol.h"
namespace EspNowLink::Internal {
std::atomic_bool initialized{true},active{true};
QueueHandle_t rx_queue=nullptr,tx_queue=nullptr,mac_queue=nullptr,ack_queue=nullptr;
TaskHandle_t task_handle=nullptr;
SemaphoreHandle_t lifecycle_mutex=nullptr;
HandlerEntry handlers[MAX_HANDLERS]{}; PeerEntry peers[MAX_PEERS]{};
PendingTransmission pending{}; SendOptions default_reliable_options{}; LinkStatistics statistics{};
uint32_t transmission_generation=1,next_sequence=1,local_session_id=1;
portMUX_TYPE statistics_lock=0,state_lock=0;
PeerEntry* find_peer(const MacAddress& address) { for(auto& p:peers) if(p.used && p.config.address==address) return &p; return nullptr; }
void increment_counter(uint32_t* p) { ++*p; }
esp_err_t encode_frame(uint8_t,uint16_t,uint32_t,uint32_t,const uint8_t*,size_t,uint8_t*,size_t,size_t* n) { *n=20; return ESP_OK; }
bool decode_frame(const uint8_t*,size_t,ParsedFrame*) { return false; }
}
namespace EspNowLink {
const MacAddress BROADCAST_ADDRESS={{255,255,255,255,255,255}};
bool MacAddress::operator==(const MacAddress& p) const { return !memcmp(bytes,p.bytes,6); }
bool MacAddress::operator!=(const MacAddress& p) const { return !(*this==p); }
bool MacAddress::is_broadcast() const { return *this==BROADCAST_ADDRESS; }
}
#include "espnow_link_api.cpp"
#include "espnow_link_task.cpp"
using namespace EspNowLink;
using namespace EspNowLink::Internal;
int callbacks=0; SendResult callback_result=SendResult::SENT;
void completed(SendResult result,uint32_t,void*) { ++callbacks; callback_result=result; }
int main() {
 tx_queue=xQueueCreate(TX_QUEUE_LENGTH,sizeof(SendRequest));
 rx_queue=xQueueCreate(RX_QUEUE_LENGTH,sizeof(RxEvent));
 mac_queue=xQueueCreate(MAC_QUEUE_LENGTH,sizeof(MacResultEvent));
 ack_queue=xQueueCreate(ACK_QUEUE_LENGTH,sizeof(AckRequest));
 PeerConfig peer{}; peer.address={{1,1,1,1,1,1}}; peer.encrypted=true; assert(add_peer(peer)==ESP_OK);
 uint8_t data=1; SendRequest request{};
 assert(send(peer.address,0x200,&data,1,{},completed)==ESP_OK);
 xQueueReceive(tx_queue,&request,0);
 cancel_transmissions_from_isr(); start_send(request);
 assert(callbacks==1 && callback_result==SendResult::CANCELLED && driver_sends==0);
 // Cancelling an in-flight frame retains driver ownership until the old MAC callback.
 assert(send(peer.address,0x200,&data,1,{},completed)==ESP_OK);
 xQueueReceive(tx_queue,&request,0); start_send(request); assert(driver_sends==1);
 cancel_transmissions(); finish_pending(SendResult::CANCELLED);
 assert(driver_owner==DriverOwner::PENDING && callbacks==2);
 process_mac_result({peer.address,true}); assert(driver_owner==DriverOwner::NONE);
 // New-generation traffic is not cancelled and old ACK queues survive cancellation.
 queue_ack(peer.address,1,1); cancel_transmissions(); assert(uxQueueMessagesWaiting(ack_queue)==1);
 assert(send(peer.address,0x200,&data,1,{},completed)==ESP_OK);
 xQueueReceive(tx_queue,&request,0); start_send(request); assert(driver_sends==2);
 active=false; cancel_transmissions(); reset_transmit_state();
 assert(driver_owner==DriverOwner::NONE && uxQueueMessagesWaiting(ack_queue)==0);
 assert(callbacks==2); // Lifecycle caller does not execute client callbacks.
 if(pending.active && pending.request.generation!=get_transmission_generation()) finish_pending(SendResult::CANCELLED);
 assert(callbacks==3 && callback_result==SendResult::CANCELLED);
 assert(send(peer.address,0x200,&data,1)==ESP_ERR_INVALID_STATE);
}
'''

REGISTRY_TEST = r'''
#include <cassert>
#include <cstring>
#include <vector>
#include "remote_switch_registry.h"
std::vector<EspNowLink::SavedPeer> saved;
namespace EspNowLink {
bool MacAddress::operator==(const MacAddress& peer) const { return !memcmp(bytes,peer.bytes,6); }
bool MacAddress::operator!=(const MacAddress& peer) const { return !(*this==peer); }
esp_err_t get_saved_peer(size_t index,SavedPeer* peer) {
 if(index>=saved.size()) return ESP_ERR_NOT_FOUND;
 *peer=saved[index]; return ESP_OK;
}
esp_err_t set_saved_peer_role(const MacAddress& address,uint8_t role) {
 for(auto& peer:saved) if(peer.address==address) { peer.role=role; return ESP_OK; }
 return ESP_ERR_NOT_FOUND;
}
}
#include "remote_switch_registry.cpp"
using namespace EspNowService;
using namespace EspNowService::RemoteRegistry;
int main() {
 EspNowLink::MacAddress a={{1,1,1,1,1,1}},b={{2,2,2,2,2,2}},stop={{3,3,3,3,3,3}};
 saved={{a,1,2},{b,1,2},{stop,1,3}}; refresh_bindings();
 assert(is_inhibited()); // A persisted emergency binding starts with unknown authorization.
 observe(a,31); ++test_tick; observe(b,82);
 RemoteSwitchStatus status{}; assert(get(a,status) && status.battery_percent==31);
 assert(get(b,status) && status.battery_percent==82);
 assert(latest(status) && status.address==b);
 set_interlock(stop,false); assert(!is_inhibited());
 set_interlock(a,true); assert(!is_inhibited()); // A button cannot impersonate an emergency role.
 test_tick+=3001; observe(stop); assert(is_inhibited()); // Telemetry is not an authorization heartbeat.
 set_interlock(stop,true); refresh_bindings(); assert(is_inhibited());
 set_interlock(stop,false); assert(!is_inhibited());
 saved.erase(saved.begin()+1); refresh_bindings(); assert(!get(b,status));
 assert(get(a,status) && status.battery_percent==31);
 // Simulate a registry restart: saved role remains, runtime authorization must be established again.
 auto bindings=saved; saved.clear(); refresh_bindings(); saved=bindings; refresh_bindings(); assert(is_inhibited());
 saved[1].role=0; saved.clear(); refresh_bindings(); saved={{stop,1,0}}; refresh_bindings();
 set_interlock(stop,true); persist_roles(); assert(saved[0].role==3 && is_inhibited());
}
'''

def main():
    with tempfile.TemporaryDirectory(prefix='pairing-host-', dir=ROOT / 'tests') as tmp:
        work = Path(tmp)
        for name, text in STUBS.items():
            path = work / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding='utf-8')
        for name, source in [('pairing', PAIRING), ('cancellation', LINK_TEST), ('registry', REGISTRY_TEST)]:
            harness = work / (name + '.cpp')
            harness.write_text(source, encoding='utf-8')
            binary = work / (name + ('.exe' if os.name == 'nt' else ''))
            proto = ROOT / 'components/product/espnow_service_proto'
            includes = [work, LINK/'include', LINK/'private_include', LINK/'src', proto/'include', proto/'src']
            command = [CXX, '-std=c++17', '-Wall', '-Wextra', *['-I'+str(p) for p in includes]]
            subprocess.run([*command, str(harness), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
            print('PASS ' + name, flush=True)

if __name__ == '__main__':
    main()
