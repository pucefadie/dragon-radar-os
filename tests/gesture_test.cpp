#include "gesture_manager.h"
#include <cassert>
int main() {
  GestureManager g;
  assert(g.update(true, 230, 420, 100).type == Gesture::None);
  assert(g.update(true, 230, 300, 200).type == Gesture::None);
  assert(g.update(false, 230, 300, 220).type == Gesture::SwipeUp);
  assert(g.update(true, 100, 100, 300).type == Gesture::None);
  assert(g.update(false, 100, 100, 400).type == Gesture::Tap);
  g.update(true, 100, 100, 500); g.update(true, 140, 100, 550); g.update(true, 100, 100, 600);
  assert(g.update(false, 100, 100, 700).type == Gesture::None);
  g.update(true, 100, 100, 800);
  assert(g.update(false, 100, 100, 1600).type == Gesture::Hold);
}
