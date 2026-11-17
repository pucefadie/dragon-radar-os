#include "standby_button.h"
#include <cassert>
int main() {
  StandbyButtonGate gate;
  assert(!gate.shouldEnter(true,2999,false));
  assert(gate.shouldEnter(true,3000,false));
  assert(!gate.shouldEnter(true,5000,true));
  assert(!gate.shouldEnter(false,5000,false));
  gate.entered(true);
  assert(!gate.shouldWake(true,false));
  assert(!gate.shouldWake(false,true));
  assert(!gate.shouldWake(true,false));
  assert(gate.shouldWake(false,true));
  gate.entered(false);
  assert(gate.shouldWake(false,true));
}
