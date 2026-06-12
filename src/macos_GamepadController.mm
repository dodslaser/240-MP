// macOS gamepad support via GameController.framework (GCController).
// Compiled as Objective-C++ and only included in the macOS build.
//
// Button→action mapping uses the same numbering as the Linux joystick API so
// the binding IDs ("btn_0", "hat_up", etc.) are interchangeable across platforms.
//   GCExtendedGamepad.buttonA  → btn_0
//   GCExtendedGamepad.buttonB  → btn_1
//   GCExtendedGamepad.buttonX  → btn_2
//   GCExtendedGamepad.buttonY  → btn_3
//   leftShoulder               → btn_4
//   rightShoulder              → btn_5
//   buttonOptions (Back/Share) → btn_6
//   buttonMenu  (Start/Menu)   → btn_7
//   dpad.up/down/left/right    → hat axis events (axes 7/6)
//   leftThumbstick  x/y        → axes 0/1
//   rightThumbstick x/y        → axes 3/4

#import <GameController/GameController.h>
#include "GamepadController.h"
#include <QDebug>

struct GamepadController::PlatformData {
    id connectObs    = nil;
    id disconnectObs = nil;
};

// All GCController handlers must run on the Qt main thread. We use
// dispatch_async(main_queue) to guarantee this regardless of which thread
// the framework chooses to deliver on.
static void applyController(GamepadController *ctrl, GCController *gc)
{
    if (!gc) return;
    GCExtendedGamepad *gp = gc.extendedGamepad;
    if (!gp) {
        qDebug("[GamepadController] Controller lacks extended gamepad profile — skipping");
        return;
    }

    // D-pad → hat axis events (axis 7 = Y, axis 6 = X)
    gp.dpad.up.pressedChangedHandler = ^(GCControllerButtonInput *, float, BOOL pressed) {
        BOOL p = pressed; GamepadController *c = ctrl;
        dispatch_async(dispatch_get_main_queue(), ^{ c->onAxisEvent(7, p ? -32767 : 0); });
    };
    gp.dpad.down.pressedChangedHandler = ^(GCControllerButtonInput *, float, BOOL pressed) {
        BOOL p = pressed; GamepadController *c = ctrl;
        dispatch_async(dispatch_get_main_queue(), ^{ c->onAxisEvent(7, p ? 32767 : 0); });
    };
    gp.dpad.left.pressedChangedHandler = ^(GCControllerButtonInput *, float, BOOL pressed) {
        BOOL p = pressed; GamepadController *c = ctrl;
        dispatch_async(dispatch_get_main_queue(), ^{ c->onAxisEvent(6, p ? -32767 : 0); });
    };
    gp.dpad.right.pressedChangedHandler = ^(GCControllerButtonInput *, float, BOOL pressed) {
        BOOL p = pressed; GamepadController *c = ctrl;
        dispatch_async(dispatch_get_main_queue(), ^{ c->onAxisEvent(6, p ? 32767 : 0); });
    };

    // Face buttons
    gp.buttonA.pressedChangedHandler = ^(GCControllerButtonInput *, float, BOOL pressed) {
        BOOL p = pressed; GamepadController *c = ctrl;
        dispatch_async(dispatch_get_main_queue(), ^{ c->onButtonEvent(0, p); });
    };
    gp.buttonB.pressedChangedHandler = ^(GCControllerButtonInput *, float, BOOL pressed) {
        BOOL p = pressed; GamepadController *c = ctrl;
        dispatch_async(dispatch_get_main_queue(), ^{ c->onButtonEvent(1, p); });
    };
    gp.buttonX.pressedChangedHandler = ^(GCControllerButtonInput *, float, BOOL pressed) {
        BOOL p = pressed; GamepadController *c = ctrl;
        dispatch_async(dispatch_get_main_queue(), ^{ c->onButtonEvent(2, p); });
    };
    gp.buttonY.pressedChangedHandler = ^(GCControllerButtonInput *, float, BOOL pressed) {
        BOOL p = pressed; GamepadController *c = ctrl;
        dispatch_async(dispatch_get_main_queue(), ^{ c->onButtonEvent(3, p); });
    };

    // Shoulder buttons
    gp.leftShoulder.pressedChangedHandler = ^(GCControllerButtonInput *, float, BOOL pressed) {
        BOOL p = pressed; GamepadController *c = ctrl;
        dispatch_async(dispatch_get_main_queue(), ^{ c->onButtonEvent(4, p); });
    };
    gp.rightShoulder.pressedChangedHandler = ^(GCControllerButtonInput *, float, BOOL pressed) {
        BOOL p = pressed; GamepadController *c = ctrl;
        dispatch_async(dispatch_get_main_queue(), ^{ c->onButtonEvent(5, p); });
    };

    // buttonOptions (Back/Share/Select) and buttonMenu (Start/Menu) — macOS 10.15+
    if (@available(macOS 10.15, *)) {
        if (gp.buttonOptions) {
            gp.buttonOptions.pressedChangedHandler = ^(GCControllerButtonInput *, float, BOOL pressed) {
                BOOL p = pressed; GamepadController *c = ctrl;
                dispatch_async(dispatch_get_main_queue(), ^{ c->onButtonEvent(6, p); });
            };
        }
        gp.buttonMenu.pressedChangedHandler = ^(GCControllerButtonInput *, float, BOOL pressed) {
            BOOL p = pressed; GamepadController *c = ctrl;
            dispatch_async(dispatch_get_main_queue(), ^{ c->onButtonEvent(7, p); });
        };
    }

    // Thumbsticks → axes 0/1 (left) and 3/4 (right).
    // GCController Y: +1 = up, -1 = down. Our convention (matching the Linux joystick
    // API): negative = up, so we invert the Y axis on both sticks.
    gp.leftThumbstick.xAxis.valueChangedHandler = ^(GCControllerAxisInput *, float value) {
        int v = (int)(value * 32767.0f); GamepadController *c = ctrl;
        dispatch_async(dispatch_get_main_queue(), ^{ c->onAxisEvent(0, v); });
    };
    gp.leftThumbstick.yAxis.valueChangedHandler = ^(GCControllerAxisInput *, float value) {
        int v = -(int)(value * 32767.0f); GamepadController *c = ctrl;
        dispatch_async(dispatch_get_main_queue(), ^{ c->onAxisEvent(1, v); });
    };
    gp.rightThumbstick.xAxis.valueChangedHandler = ^(GCControllerAxisInput *, float value) {
        int v = (int)(value * 32767.0f); GamepadController *c = ctrl;
        dispatch_async(dispatch_get_main_queue(), ^{ c->onAxisEvent(3, v); });
    };
    gp.rightThumbstick.yAxis.valueChangedHandler = ^(GCControllerAxisInput *, float value) {
        int v = -(int)(value * 32767.0f); GamepadController *c = ctrl;
        dispatch_async(dispatch_get_main_queue(), ^{ c->onAxisEvent(4, v); });
    };

    ctrl->setConnected(true);
    const char *name = gc.vendorName ? [gc.vendorName UTF8String] : "(unnamed)";
    qDebug("[GamepadController] Controller connected: %s", name);
}

void GamepadController::platformInit()
{
    m_pd = new PlatformData;

    // Pick up any already-connected controllers
    for (GCController *gc in [GCController controllers]) {
        applyController(this, gc);
        break; // use first controller only
    }

    GamepadController *self = this;

    m_pd->connectObs = [[NSNotificationCenter defaultCenter]
        addObserverForName:GCControllerDidConnectNotification
        object:nil
        queue:[NSOperationQueue mainQueue]
        usingBlock:^(NSNotification *note) {
            applyController(self, (GCController *)note.object);
        }];

    m_pd->disconnectObs = [[NSNotificationCenter defaultCenter]
        addObserverForName:GCControllerDidDisconnectNotification
        object:nil
        queue:[NSOperationQueue mainQueue]
        usingBlock:^(NSNotification *) {
            self->setConnected(false);
        }];
}

void GamepadController::platformShutdown()
{
    if (!m_pd) return;
    NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
    if (m_pd->connectObs)    [nc removeObserver:m_pd->connectObs];
    if (m_pd->disconnectObs) [nc removeObserver:m_pd->disconnectObs];
    delete m_pd;
    m_pd = nullptr;
}
