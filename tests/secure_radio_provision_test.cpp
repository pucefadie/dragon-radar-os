#include "secure_radio_provision.h"
#include <array>
#include <cassert>
#include <string>

namespace {
unsigned opens=0,writes=0,commits=0,closes=0;
int presence=ESP_ERR_NVS_NOT_FOUND,commitResult=ESP_OK;
std::array<uint8_t,SECURE_CONFIG_SIZE> staged{};
}

int nvs_open(const char *name,int mode,nvs_handle_t *handle) {
  assert(std::strcmp(name,SECURE_RECORD_NAMESPACE)==0 && mode==NVS_READWRITE);
  *handle=1;
  ++opens;
  return ESP_OK;
}
int nvs_find_key(nvs_handle_t handle,const char *key,void *) {
  assert(handle==1 && std::strcmp(key,SECURE_RECORD_V2_KEY)==0);
  return presence;
}
int nvs_set_blob(nvs_handle_t handle,const char *key,const void *value,size_t size) {
  assert(handle==1 && std::strcmp(key,SECURE_RECORD_V2_KEY)==0 && size==staged.size());
  std::memcpy(staged.data(),value,size);
  ++writes;
  return ESP_OK;
}
int nvs_commit(nvs_handle_t handle) {
  assert(handle==1);
  ++commits;
  return commitResult;
}
void nvs_close(nvs_handle_t handle) {
  assert(handle==1);
  ++closes;
}

int main(int argc,char **argv) {
  assert(argc==2);
  const std::string test=argv[1];
  std::array<uint8_t,SECURE_CONFIG_SIZE> record{{'D','R','C','2',1,6,2}};
  record[12]=5; record[28]=8; record[60]=9;
  if (test=="legacy") record[3]='1';
  if (test=="exists") presence=ESP_OK; // An existing value of any type blocks a write.
  if (test=="lookup-error") presence=-1;
  if (test=="commit-error") commitResult=-1;

  Stream console;
  console.record=record.data();
  console.recordSize=record.size();
  secureRadioProvisionPoll(console);
  const bool written=test=="install" || test=="commit-error";
  assert(writes==unsigned(written) && commits==unsigned(written));
  assert(opens==unsigned(test!="legacy") && closes==opens);
  if (written) assert(staged==record);
  assert(console.reply==(test=="install"?"RADIO_PROVISION_OK":"RADIO_PROVISION_REJECTED"));
  console.command='P';
  secureRadioProvisionPoll(console);
  assert(console.replies==1 && writes==unsigned(written));
}
