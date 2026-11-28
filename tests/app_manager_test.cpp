#include "app_manager.h"
#include <cassert>
int main() {
  GestureEvent motorTap;
  motorTap.type=Gesture::Tap;
  motorTap.x=DEBUG_SPHERE_MOTOR_X;
  motorTap.y=DEBUG_SPHERE_MOTOR_Y;
  assert(debugSphereMotorTap(motorTap));
  motorTap.type=Gesture::Hold;
  assert(!debugSphereMotorTap(motorTap));
  motorTap.type=Gesture::Tap;
  motorTap.y=355;
  assert(!debugSphereMotorTap(motorTap));
  AppManager a;
  assert(a.active() == AppId::Radar && !a.launcherOpen());
  GestureEvent e; e.type = Gesture::SwipeUp; e.x = 230; e.y = 420; a.gesture(e);
  assert(a.launcherOpen());
  e.type = Gesture::Tap; e.x = 300; e.y = 150; a.gesture(e);
  assert(a.active() == AppId::Audiobookshelf && !a.launcherOpen());
  a.longPress(); assert(a.launcherOpen());
  assert(a.selection() == 1);
  a.shortPress(); assert(a.selection() == 2);
  a.longPress(); assert(a.active() == AppId::Settings && !a.launcherOpen());
  a.longPress(); assert(a.launcherOpen());
  e.type = Gesture::SwipeDown; a.gesture(e);
  assert(!a.launcherOpen());
  a.longPress(); e.type=Gesture::Tap; e.x=300; e.y=270; a.gesture(e);
  assert(a.active()==AppId::Debug && a.debugPage()==0);
  a.shortPress(); assert(a.debugPage()==1);
  for (unsigned i=1;i<DEBUG_PAGE_COUNT;++i) a.shortPress();
  assert(a.debugPage()==0);

  // The touchscreen launcher must select the current app, just like longPress.
  e.type=Gesture::SwipeUp; e.x=230; e.y=420; a.gesture(e);
  assert(a.launcherOpen() && a.selection()==3);
  a.longPress();
  assert(a.active()==AppId::Debug && !a.launcherOpen());
  a.showDebugPage(2);
  a.gesture(e);
  assert(a.selection()==3);
  a.shortPress();
  assert(a.selection()==0);
  a.longPress();
  assert(a.active()==AppId::Radar && !a.launcherOpen());

  for (uint8_t id=0;id<4;++id) {
    const auto x=launcherTileX(id), y=launcherTileY(id);
    assert(launcherTileAt(x,y)==id);
    assert(launcherTileAt(x+LAUNCHER_TILE_WIDTH-1,y+LAUNCHER_TILE_HEIGHT-1)==id);
    assert(launcherTileAt(x+LAUNCHER_TILE_WIDTH,y)==-1);
    assert(launcherTileAt(x,y+LAUNCHER_TILE_HEIGHT)==-1);
  }
  a.longPress();
  e.type=Gesture::Tap; e.x=233; e.y=150; a.gesture(e);
  assert(a.launcherOpen() && a.active()==AppId::Radar);
  e.x=300; e.y=230; a.gesture(e);
  assert(a.launcherOpen() && a.active()==AppId::Radar);
  e.y=270; a.gesture(e);
  assert(!a.launcherOpen() && a.active()==AppId::Debug);
}
