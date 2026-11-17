#include "sd_service.h"
#include "hw_config.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include <atomic>
namespace {
std::atomic<SdServiceState> state{SdServiceState::NotChecked};
std::atomic<uint32_t> totalMb{0}, freeMb{0};
void checkTask(void *) {
  bool ok=SD_MMC.setPins(HW_SD_CLK_PIN,HW_SD_CMD_PIN,HW_SD_D0_PIN)
      && SD_MMC.begin("/sdcard",true,false,SDMMC_FREQ_DEFAULT,5)
      && SD_MMC.cardType()!=CARD_NONE;
  if (ok) {
    uint64_t total=SD_MMC.totalBytes(), used=SD_MMC.usedBytes();
    ok=total>0 && used<=total;
    if(ok) { totalMb=(uint32_t)(total/(1024ULL*1024ULL)); freeMb=(uint32_t)((total-used)/(1024ULL*1024ULL)); }
  }
  if (!ok) { SD_MMC.end(); totalMb=0; freeMb=0; }
  state=ok ? SdServiceState::Mounted : SdServiceState::Unavailable;
  Serial.printf("[SD] %s total=%lu MiB free=%lu MiB (nessuna formattazione)\n",ok?"MONTATA":"NON DISPONIBILE",(unsigned long)totalMb.load(),(unsigned long)freeMb.load());
  vTaskDelete(nullptr);
}
}
bool sdServiceRequestCheck() {
  SdServiceState current=state.load();
  if (current==SdServiceState::Mounted || current==SdServiceState::Checking) return false;
  if (!state.compare_exchange_strong(current,SdServiceState::Checking)) return false;
  if(xTaskCreate(checkTask,"sd_check",6144,nullptr,1,nullptr)!=pdPASS) { state=SdServiceState::Unavailable; return false; }
  return true;
}
SdServiceState sdServiceState() { return state.load(); }
const char *sdServiceStatusText() {
  switch(state.load()) {
    case SdServiceState::Checking: return "CONTROLLO";
    case SdServiceState::Mounted: return "MONTATA";
    case SdServiceState::Unavailable: return "NON LETTA";
    default: return "CONTROLLA";
  }
}
uint32_t sdServiceTotalMB() { return totalMb.load(); }
uint32_t sdServiceFreeMB() { return freeMb.load(); }
