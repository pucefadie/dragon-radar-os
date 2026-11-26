#include <Arduino.h>
#include <nvs_flash.h>
#if SPHERE_OTA
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_partition.h>
#include <atomic>
#include "secure_radio.h"
#include "sphere_nvs_guard.h"
#if SPHERE_OTA_NVS_TRACE
#include "sphere_nvs_trace.h"
#endif
#ifndef DRAGON_BALL_ID
#include "beacon_config.h"
#endif
#endif

#if !CONFIG_IDF_TARGET_ESP32C6 || !M1C_C6_NVS_GUARD || RADIO_DEV_INSECURE
#error "Build this startup only for the authenticated C6 M1c bench."
#endif

#if !M1C_C6_PROVISION
#include <WiFi.h>
#if defined(ARDUINO_XIAO_ESP32C6) && ARDUINO_USB_CDC_ON_BOOT
#include "sphere_motor_gpio.h"
#endif
#endif

#if SPHERE_OTA
namespace {
std::atomic<bool> nvsUnlocked{false};
std::atomic<unsigned> blockedMutations{0};
std::atomic<uint32_t> firstMutation{0};
std::atomic<uintptr_t> firstCaller{0};
std::atomic<unsigned> nvsPhase{0};

esp_err_t flashReadCheck=ESP_ERR_INVALID_STATE;
esp_err_t initResult=ESP_ERR_INVALID_STATE, openResult=ESP_ERR_INVALID_STATE;
esp_err_t keyResult=ESP_ERR_INVALID_STATE, readResult=ESP_ERR_INVALID_STATE;
bool recordValid=false;
uint8_t recordVersion=0;
}

extern "C" esp_err_t __real_esp_partition_write(const esp_partition_t *,size_t,const void *,size_t);
extern "C" esp_err_t __real_esp_partition_write_raw(const esp_partition_t *,size_t,const void *,size_t);
extern "C" esp_err_t __real_esp_partition_erase_range(const esp_partition_t *,size_t,size_t);

static bool rejectMutation(const esp_partition_t *partition,size_t relative,unsigned operation,void *caller) {
  if (!partition || !sphereNvsMutationBlocked(nvsUnlocked.load(),partition->address,
      partition->type==ESP_PARTITION_TYPE_DATA && partition->subtype==ESP_PARTITION_SUBTYPE_DATA_NVS)) return false;
  if (blockedMutations.fetch_add(1)==0) {
    const auto address=uint32_t(partition->address+relative);
    firstCaller.store(reinterpret_cast<uintptr_t>(caller));
    firstMutation.store((operation<<28)|(nvsPhase.load()<<24)|(address&0xffffff));
  }
  return true;
}
extern "C" esp_err_t __wrap_esp_partition_write(const esp_partition_t *p,size_t off,const void *buf,size_t n) {
  return rejectMutation(p,off,1,__builtin_return_address(0)) ? ESP_ERR_INVALID_STATE : __real_esp_partition_write(p,off,buf,n);
}
extern "C" esp_err_t __wrap_esp_partition_write_raw(const esp_partition_t *p,size_t off,const void *buf,size_t n) {
  return rejectMutation(p,off,2,__builtin_return_address(0)) ? ESP_ERR_INVALID_STATE : __real_esp_partition_write_raw(p,off,buf,n);
}
extern "C" esp_err_t __wrap_esp_partition_erase_range(const esp_partition_t *p,size_t off,size_t n) {
  return rejectMutation(p,off,3,__builtin_return_address(0)) ? ESP_ERR_INVALID_STATE : __real_esp_partition_erase_range(p,off,n);
}

static SecureRecordRead earlyRead(void *storage,const char *key,uint8_t *bytes,size_t &length) {
  const auto handle=*static_cast<nvs_handle_t *>(storage);
  nvs_type_t type;
  keyResult=nvs_find_key(handle,key,&type);
  if (keyResult==ESP_ERR_NVS_NOT_FOUND) return SecureRecordRead::Missing;
  if (keyResult!=ESP_OK || type!=NVS_TYPE_BLOB) {
    readResult=keyResult==ESP_OK ? ESP_ERR_NVS_TYPE_MISMATCH : keyResult;
    return SecureRecordRead::Error;
  }
  readResult=nvs_get_blob(handle,key,bytes,&length);
  return readResult==ESP_OK ? SecureRecordRead::Loaded : SecureRecordRead::Error;
}

#if !SPHERE_OTA_NVS_TRACE
// A mismatched loader can return successful raw reads with wrong contents.
static esp_err_t verifyFlashReadPath(const esp_partition_t *partition) {
  const void *mapped=nullptr;
  esp_partition_mmap_handle_t handle{};
  auto result=esp_partition_mmap(partition,0,partition->size,ESP_PARTITION_MMAP_DATA,&mapped,&handle);
  if (result!=ESP_OK) return result;
  uint8_t bytes[256];
  for (size_t offset=0;offset<partition->size;offset+=sizeof(bytes)) {
    result=esp_partition_read_raw(partition,offset,bytes,sizeof(bytes));
    if (result!=ESP_OK) break;
    if (memcmp(bytes,static_cast<const uint8_t *>(mapped)+offset,sizeof(bytes))!=0) {
      result=ESP_ERR_INVALID_CRC;
      break;
    }
  }
  esp_partition_munmap(handle);
  return result;
}
#endif

static void inspectNvs() {
  const auto *partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_NVS,"nvs");
  if (!partition || partition->address!=0x9000 || partition->size!=0x5000) return;
#if !SPHERE_OTA_NVS_TRACE
  flashReadCheck=verifyFlashReadPath(partition);
  if (flashReadCheck!=ESP_OK) return;
#endif
  nvsPhase.store(1);
#if SPHERE_OTA_NVS_TRACE
  sphereNvsTrace::recordingMount=true;
#endif
  initResult=nvs_flash_init();
#if SPHERE_OTA_NVS_TRACE
  sphereNvsTrace::recordingMount=false;
  sphereNvsTrace::read(partition);
#endif
  if (initResult!=ESP_OK) return;
  nvs_handle_t storage;
  nvsPhase.store(2);
  openResult=nvs_open(SECURE_RECORD_NAMESPACE,NVS_READONLY,&storage);
  if (openResult!=ESP_OK) return;
  SecureRadioConfig config{};
  nvsPhase.store(3);
  recordValid=secureRadioLoadConfig(earlyRead,&storage,config,DRAGON_BALL_ID);
  if (recordValid) recordVersion=uint8_t(config.version);
  secureRadioClear(&config,sizeof(config));
  nvs_close(storage);
  nvsPhase.store(4);
}

bool sphereNvsReady() { return recordValid; }
void sphereNvsUnlock() {
#if !SPHERE_OTA_NVS_TRACE
  if (recordValid) nvsUnlocked.store(true);
#endif
}
void sphereNvsPrint(Stream &out) {
  const uint32_t mutation=firstMutation.load();
  out.printf("M3E_NVS init=%d open=%d key=%d read=%d valid=%u version=%u blocked=%u unlocked=%u first_op=%u first_phase=%u first_address=%06lx first_pc=%08lx flash_read_check=%d\n",
      int(initResult),int(openResult),int(keyResult),int(readResult),unsigned(recordValid),
      unsigned(recordVersion),blockedMutations.load(),unsigned(nvsUnlocked.load()),
      unsigned(mutation>>28),unsigned((mutation>>24)&15),
      (unsigned long)(mutation&0xffffff),(unsigned long)firstCaller.load(),int(flashReadCheck));
#if SPHERE_OTA_NVS_TRACE
  sphereNvsTrace::print(out);
#endif
}

static esp_timer_handle_t bootDeadline;
static void restartUnverified(void *) { esp_restart(); }
void sphereOtaCancelBootDeadline() {
  if (bootDeadline) {
    esp_timer_stop(bootDeadline);
    esp_timer_delete(bootDeadline);
    bootDeadline=nullptr;
  }
}
#endif

extern "C" void __wrap_initArduino() {
#if !M1C_C6_PROVISION && defined(ARDUINO_XIAO_ESP32C6) && ARDUINO_USB_CDC_ON_BOOT
  sphereMotorSafeLow();
#endif
#if SPHERE_OTA
  esp_ota_img_states_t appState;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(),&appState)==ESP_OK &&
      appState==ESP_OTA_IMG_PENDING_VERIFY) {
    esp_timer_create_args_t deadline{};
    deadline.callback=restartUnverified;
    deadline.name="sphere_ota_boot";
    if (esp_timer_create(&deadline,&bootDeadline)!=ESP_OK ||
        esp_timer_start_once(bootDeadline,10000000)!=ESP_OK)
      esp_ota_mark_app_invalid_rollback_and_reboot();
  }
#endif
#if SPHERE_OTA
  inspectNvs();
#else
  const esp_err_t result = nvs_flash_init();
  if (result != ESP_OK) {
#if SPHERE_OTA
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(),&state)==ESP_OK &&
        state==ESP_OTA_IMG_PENDING_VERIFY) esp_ota_mark_app_invalid_rollback_and_reboot();
#endif
    Serial.begin(115200);
    for (;;) {
      Serial.printf("M1C_C6_NVS_GUARD=1 NVS_INIT_FAILED=%d\n", int(result));
      delay(1000);
    }
  }
  Serial.println("M1C_C6_NVS_GUARD=1");
#endif
#if !M1C_C6_PROVISION
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
#endif
}
