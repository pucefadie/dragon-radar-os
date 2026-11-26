#include "../examples/dragon_ball_beacon/sphere_nvs_guard.h"

static_assert(sphereNvsMutationBlocked(false,0x9000,false),"Protect the installed NVS offset");
static_assert(sphereNvsMutationBlocked(false,0x1000,true),"Protect other NVS partitions");
static_assert(sphereNvsMutationBlocked(false,0x9000,true),"Block NVS mutations before proof");
static_assert(!sphereNvsMutationBlocked(false,0xe000,false),"Allow OTA metadata");
static_assert(!sphereNvsMutationBlocked(false,0x1d0000,false),"Allow OTA app writes");
static_assert(!sphereNvsMutationBlocked(true,0x9000,true),"Unlock after proof");

int main() {}
