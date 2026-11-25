#include "nvs_storage.hpp"
#include "partition.hpp"
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include <iostream>

class Flash : public nvs::Partition {
public:
  std::vector<uint8_t> bytes;
  unsigned erases=0, writes=0;
  explicit Flash(std::vector<uint8_t> input):bytes(std::move(input)) {}
  const char *get_partition_name() override { return "nvs"; }
  uint32_t get_address() override { return 0x9000; }
  uint32_t get_size() override { return bytes.size(); }
  bool get_readonly() override { return false; }
  esp_err_t read_raw(size_t off,void *dst,size_t n) override {
    if (off>bytes.size() || n>bytes.size()-off) return ESP_ERR_INVALID_ARG;
    std::memcpy(dst,bytes.data()+off,n);
    return ESP_OK;
  }
  esp_err_t read(size_t off,void *dst,size_t n) override { return read_raw(off,dst,n); }
  esp_err_t write_raw(size_t off,const void *,size_t n) override {
    ++writes;
    std::cout<<"write offset="<<off<<" length="<<n<<"\n";
    return ESP_ERR_INVALID_STATE;
  }
  esp_err_t write(size_t off,const void *buf,size_t n) override { return write_raw(off,buf,n); }
  esp_err_t erase_range(size_t off,size_t n) override {
    ++erases;
    std::cout<<"erase offset="<<off<<" length="<<n<<"\n";
    return ESP_ERR_INVALID_STATE;
  }
};

int main(int argc,char **argv) {
  if (argc<2 || argc>3) return 1;
  std::ifstream f(argv[1],std::ios::binary);
  Flash flash({std::istreambuf_iterator<char>(f),{}});
  if (flash.bytes.size()!=0x5000) return 2;

  for (unsigned i=0;i<5;++i) {
    nvs::Page page;
    const auto result=page.load(&flash,i);
    uint32_t seq=0;
    const auto seqResult=page.getSeqNumber(seq);
    std::cout<<"page="<<i<<" load="<<result<<" state=0x"<<std::hex<<uint32_t(page.state())<<std::dec
             <<" sequence="<<seq<<" sequence_result="<<seqResult<<"\n";
  }
  nvs::Storage store(&flash);
  const auto mount=store.init(0,5);
  std::cout<<"mount="<<mount<<" erases="<<flash.erases<<" writes="<<flash.writes<<"\n";
  if (mount!=ESP_OK) return 3;
  uint8_t ns=0;
  const auto opened=store.createOrOpenNamespace("dragon_radio",false,ns);
  uint8_t record[76]{};
  const auto read=opened==ESP_OK?store.readItem(ns,nvs::ItemType::BLOB,"peer_v1",record,sizeof(record)):opened;
  std::cout<<"namespace="<<opened<<" DRC1 read="<<read<<" valid_header="
           <<(read==ESP_OK && !std::memcmp(record,"DRC1\1",5))<<"\n";
  if (read!=ESP_OK) return 4;
  if (argc==3) {
    std::ifstream reference(argv[2],std::ios::binary);
    const std::vector<uint8_t> expected{std::istreambuf_iterator<char>(reference),{}};
    const bool same=expected.size()==sizeof(record) && !std::memcmp(record,expected.data(),sizeof(record));
    std::cout<<"DRC1 byte-for-byte="<<same<<"\n";
    if (!same) return 5;
  }
  return 0;
}
