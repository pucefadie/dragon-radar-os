#include "sphere_motor.h"
#include "sphere_rgb.h"
#include <cassert>

struct TestOutput {
  bool beginOk = true, startOk = true, stopOk = true;
  bool high = false;
  unsigned pulses = 0, stops = 0;
  uint32_t now = 0, began = 0;
  uint16_t duration = 0;

  bool begin() { high = false; return beginOk; }
  bool start(uint16_t durationMs) {
    if (!startOk) return false;
    high = true;
    began = now;
    duration = durationMs;
    ++pulses;
    return true;
  }
  bool stop() { high = false; ++stops; return stopOk; }
  void advance(uint32_t time) {
    now = time;
    if (high && uint32_t(now - began) >= duration) high = false;
  }
};

int main() {
  TestOutput out;
  SphereMotor<TestOutput> motor(out);
  assert(motor.request(0) == MotorTestResult::Disabled);
  assert(motor.begin() && !out.high && out.pulses == 0);
  assert(motor.request(0) == MotorTestResult::Started && out.high);
  assert(motor.request(1) == MotorTestResult::Busy && out.pulses == 1);
  out.advance(119);
  assert(out.high);
  out.advance(120);  // RMT finishes without a console or controller tick.
  assert(!out.high);
  motor.tick(249);
  assert(out.stops == 0);
  motor.tick(250);
  assert(!out.high && out.stops == 1);
  assert(motor.request(3249) == MotorTestResult::CoolingDown);
  out.advance(3250);
  assert(motor.request(3250) == MotorTestResult::Started && out.pulses == 2);
  motor.disable();  // Logical shutdown cancels an active output.
  assert(!out.high && motor.request(7000) == MotorTestResult::Disabled);

  TestOutput startupOutput;
  SphereMotor<TestOutput> startup(startupOutput);
  assert(startup.begin() && startupOutput.pulses == 0);
  assert(startup.request(0, kMotorStartupPulseMs) == MotorTestResult::Started);
  startupOutput.advance(59);
  assert(startupOutput.high);
  startupOutput.advance(60);
  assert(!startupOutput.high && startupOutput.pulses == 1);
  assert(startup.request(100) == MotorTestResult::Busy);
  startup.tick(250);
  assert(startup.request(3249) == MotorTestResult::CoolingDown);
  startupOutput.advance(3250);
  assert(startup.request(3250) == MotorTestResult::Started && startupOutput.pulses == 2);
  assert(startupOutput.duration == 120);

  TestOutput failed;
  failed.beginOk = false;
  SphereMotor<TestOutput> initError(failed);
  assert(!initError.begin() && !failed.high);
  assert(initError.request(0) == MotorTestResult::Disabled && failed.pulses == 0);

  failed.beginOk = true;
  failed.startOk = false;
  SphereMotor<TestOutput> startError(failed);
  assert(startError.begin() && startError.request(0) == MotorTestResult::Disabled);
  assert(!failed.high && failed.stops == 1);
  failed.startOk = true;
  assert(startError.request(10000) == MotorTestResult::Disabled);

  TestOutput wrapOutput;
  wrapOutput.now = UINT32_MAX - 100;
  SphereMotor<TestOutput> wrap(wrapOutput);
  assert(wrap.begin() && wrap.request(wrapOutput.now) == MotorTestResult::Started);
  wrapOutput.advance(19);
  assert(!wrapOutput.high);
  wrap.tick(149);
  assert(wrapOutput.stops == 1 && wrap.request(3148) == MotorTestResult::CoolingDown);
  wrapOutput.now = 3149;
  assert(wrap.request(3149) == MotorTestResult::Started);
  wrapOutput.stopOk = false;
  wrap.tick(3399);
  assert(!wrapOutput.high && wrap.request(6400) == MotorTestResult::Disabled);

  MotorTestCommand parser;
  const auto lines = [&](const char *bytes) {
    unsigned requests = 0;
    while (*bytes) if (parser.consume(*bytes++)) ++requests;
    return requests;
  };
  assert(lines("motor_test\r\n") == 1);
  assert(lines("motor_test 250\nmotor_testmotor_test\n motor_test\n") == 0);
  assert(lines("bad_command_that_cannot_become_valid_motor_test\n") == 0);
  assert(lines("motor_") == 0);
  parser.reset();  // A partial command must not carry over to a new USB session.
  assert(lines("test\n") == 0);
  assert(lines("motor_test\nmotor_test\n") == 2);

  static_assert(sphereRgb::kChain.dataPin == 23 && sphereRgb::kChain.pixelCount == 3 &&
                !sphereRgb::kChain.enabled, "External pixels must stay disabled");
}
