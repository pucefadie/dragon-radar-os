#pragma once
enum class AudioResult { Started, Busy, Failed };
AudioResult audioServiceTestTone();
AudioResult audioServiceRadarBeep();
const char *audioServiceStatus();
