#pragma once
#include <Arduino.h>
#include <esp_flash.h>
#include <esp_partition.h>
#include <esp_rom_crc.h>

namespace sphereNvsTrace {
constexpr size_t pageSize=4096;
constexpr unsigned pageCount=5;
struct Page {
  uint32_t state=0, sequence=0, headerCrc=0, romCrc=0, referenceCrc=0;
  uint32_t rawCrc=0, directCrc=0, mappedCrc=0;
  int raw=ESP_ERR_INVALID_STATE, direct=ESP_ERR_INVALID_STATE, mapped=ESP_ERR_INVALID_STATE;
  int load=ESP_ERR_INVALID_STATE, seqStatus=ESP_ERR_INVALID_STATE;
  uint32_t driverSequence=0, sdkHeaderState=0, sdkHeaderSeq=0, sdkHeaderCrc=0, sdkRomCrc=0;
  int sdkRead=ESP_ERR_INVALID_STATE;
  bool directEqual=false, mappedEqual=false;
};
static Page pages[pageCount];
static const void *object[pageCount]{};
static uintptr_t eraseCaller=0;
static int erasePage=-1;
static bool recordingMount=false;

static uint32_t referenceCrc32(uint32_t seed,const uint8_t *bytes,size_t size) {
  uint32_t state=~seed;
  while (size--) {
    state^=*bytes++;
    for (unsigned bit=0;bit<8;++bit)
      state=(state>>1)^((state&1) ? 0xedb88320u : 0);
  }
  return ~state;
}

static void read(const esp_partition_t *partition) {
  static uint8_t raw[pageSize],direct[pageSize];
  const void *mapped=nullptr;
  esp_partition_mmap_handle_t handle{};
  const int map=esp_partition_mmap(partition,0,pageSize*pageCount,ESP_PARTITION_MMAP_DATA,&mapped,&handle);
  for (unsigned i=0;i<pageCount;++i) {
    auto &page=pages[i];
    const size_t offset=i*pageSize;
    page.raw=esp_partition_read_raw(partition,offset,raw,sizeof(raw));
    page.direct=esp_flash_read(partition->flash_chip,direct,partition->address+offset,sizeof(direct));
    page.mapped=map;
    if (page.raw==ESP_OK) {
      memcpy(&page.state,raw,4);
      memcpy(&page.sequence,raw+4,4);
      memcpy(&page.headerCrc,raw+28,4);
      page.romCrc=esp_rom_crc32_le(0xffffffff,raw+4,24);
      page.referenceCrc=referenceCrc32(0xffffffff,raw+4,24);
      page.rawCrc=referenceCrc32(0,raw,sizeof(raw));
    }
    if (page.direct==ESP_OK) {
      page.directCrc=referenceCrc32(0,direct,sizeof(direct));
      page.directEqual=page.raw==ESP_OK && memcmp(raw,direct,sizeof(raw))==0;
    }
    if (map==ESP_OK) {
      const auto *data=static_cast<const uint8_t *>(mapped)+offset;
      page.mappedCrc=referenceCrc32(0,data,sizeof(raw));
      page.mappedEqual=page.raw==ESP_OK && memcmp(raw,data,sizeof(raw))==0;
    }
  }
  if (map==ESP_OK) esp_partition_munmap(handle);
}

static void print(Stream &out) {
  for (unsigned i=0;i<pageCount;++i) {
    const auto &p=pages[i];
    out.printf("M3E_TRACE mount_page=%u read=%d state=%08lx seq=%lu stored=%08lx computed=%08lx\n",
      i,p.sdkRead,(unsigned long)p.sdkHeaderState,(unsigned long)p.sdkHeaderSeq,
      (unsigned long)p.sdkHeaderCrc,(unsigned long)p.sdkRomCrc);
    out.printf("M3E_TRACE page=%u raw=%d direct=%d mapped=%d state=%08lx seq=%lu header=%08lx rom=%08lx ref=%08lx raw_crc=%08lx direct_crc=%08lx mapped_crc=%08lx direct_equal=%u mapped_equal=%u load=%d seq_result=%d sdk_seq=%lu\n",
      i,p.raw,p.direct,p.mapped,(unsigned long)p.state,(unsigned long)p.sequence,(unsigned long)p.headerCrc,
      (unsigned long)p.romCrc,(unsigned long)p.referenceCrc,(unsigned long)p.rawCrc,
      (unsigned long)p.directCrc,(unsigned long)p.mappedCrc,unsigned(p.directEqual),unsigned(p.mappedEqual),
      p.load,p.seqStatus,(unsigned long)p.driverSequence);
  }
  out.printf("M3E_TRACE erase_page=%d erase_branch_pc=%08lx\n",erasePage,(unsigned long)eraseCaller);
}
}

extern "C" esp_err_t __real_esp_partition_read_raw(const esp_partition_t *, size_t,void *,size_t);
extern "C" esp_err_t __wrap_esp_partition_read_raw(const esp_partition_t *p,size_t off,void *dst,size_t n) {
  const esp_err_t result=__real_esp_partition_read_raw(p,off,dst,n);
  if (sphereNvsTrace::recordingMount && p && p->address==0x9000 && n==32 &&
      off%sphereNvsTrace::pageSize==0 && off/sphereNvsTrace::pageSize<sphereNvsTrace::pageCount) {
    auto &page=sphereNvsTrace::pages[off/sphereNvsTrace::pageSize];
    page.sdkRead=result;
    if (result==ESP_OK) {
      const auto *header=static_cast<const uint8_t *>(dst);
      memcpy(&page.sdkHeaderState,header,4);
      memcpy(&page.sdkHeaderSeq,header+4,4);
      memcpy(&page.sdkHeaderCrc,header+28,4);
      page.sdkRomCrc=esp_rom_crc32_le(0xffffffff,header+4,24);
    }
  }
  return result;
}

extern "C" int __real__ZN3nvs4Page4loadEPNS_9PartitionEm(void *,void *,unsigned long);
extern "C" int __real__ZNK3nvs4Page12getSeqNumberERm(const void *,unsigned long *);
extern "C" int __real__ZN3nvs4Page5eraseEv(void *);

extern "C" int __wrap__ZN3nvs4Page4loadEPNS_9PartitionEm(void *self,void *partition,unsigned long index) {
  const int result=__real__ZN3nvs4Page4loadEPNS_9PartitionEm(self,partition,index);
  if (index<sphereNvsTrace::pageCount) {
    sphereNvsTrace::object[index]=self;
    sphereNvsTrace::pages[index].load=result;
  }
  return result;
}

extern "C" int __wrap__ZNK3nvs4Page12getSeqNumberERm(const void *self,unsigned long *sequence) {
  const int result=__real__ZNK3nvs4Page12getSeqNumberERm(self,sequence);
  for (unsigned i=0;i<sphereNvsTrace::pageCount;++i) if (sphereNvsTrace::object[i]==self) {
    sphereNvsTrace::pages[i].seqStatus=result;
    if (result==ESP_OK) sphereNvsTrace::pages[i].driverSequence=*sequence;
    break;
  }
  return result;
}

extern "C" int __wrap__ZN3nvs4Page5eraseEv(void *self) {
  if (!sphereNvsTrace::eraseCaller) {
    sphereNvsTrace::eraseCaller=reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    for (unsigned i=0;i<sphereNvsTrace::pageCount;++i)
      if (sphereNvsTrace::object[i]==self) sphereNvsTrace::erasePage=int(i);
  }
  return __real__ZN3nvs4Page5eraseEv(self);
}
