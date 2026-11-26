#pragma once
#include <cstddef>

using nvs_handle_t=unsigned;
constexpr int ESP_OK=0;
constexpr int ESP_ERR_NVS_NOT_FOUND=1;
constexpr int NVS_READWRITE=2;
int nvs_open(const char *name,int mode,nvs_handle_t *handle);
int nvs_find_key(nvs_handle_t handle,const char *key,void *type);
int nvs_set_blob(nvs_handle_t handle,const char *key,const void *value,size_t size);
int nvs_commit(nvs_handle_t handle);
void nvs_close(nvs_handle_t handle);
