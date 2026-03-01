/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/UInputScreen.h"

#include "base/IEventQueue.h"
#include "base/Log.h"
#include "common/Settings.h"
#include "platform/UInputEventQueueBuffer.h"
#include "platform/UInputKeyState.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <linux/input.h>
#include <linux/uinput.h>
#include <string>
#include <unistd.h>

namespace deskflow {

UInputScreen::UInputScreen(bool isPrimary, IEventQueue *events)
    : PlatformScreen{events},
      m_isPrimary{isPrimary},
      m_events{events},
      m_isOnScreen{isPrimary}
{
  detectScreenSize();
  initUInput();
  m_keyState = new UInputKeyState(this, events);

  // install event handlers
  m_events->addHandler(EventTypes::System, m_events->getSystemTarget(), [this](const auto &e) {
    handleSystemEvent(e);
  });

  // install the platform event queue buffer
  m_events->adoptBuffer(nullptr);
  m_events->adoptBuffer(new UInputEventQueueBuffer(m_events));
}

UInputScreen::~UInputScreen()
{
  m_events->adoptBuffer(nullptr);
  m_events->removeHandler(EventTypes::System, m_events->getSystemTarget());

  cleanupUInput();

  delete m_keyState;
}

void UInputScreen::detectScreenSize()
{
  // Try to read the framebuffer virtual size from sysfs.
  // This gives us a reasonable screen size even without a display server.
  std::ifstream vsize("/sys/class/graphics/fb0/virtual_size");
  if (vsize.is_open()) {
    char comma;
    std::int32_t w = 0, h = 0;
    if (vsize >> w >> comma >> h && w > 0 && h > 0) {
      m_w = w;
      m_h = h;
      LOG_INFO("uinput: detected framebuffer size %dx%d", m_w, m_h);
      return;
    }
  }

  // Fallback: try DRM connector modes
  // Check /sys/class/drm/card*-*/modes for the first available mode
  // This works on systems with DRM but no fbdev
  const std::string drmPath = "/sys/class/drm/";
  // Simple fallback: check common resolution from environment
  if (const char *res = std::getenv("DESKFLOW_SCREEN_SIZE")) {
    std::int32_t w = 0, h = 0;
    if (sscanf(res, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
      m_w = w;
      m_h = h;
      LOG_INFO("uinput: using screen size from DESKFLOW_SCREEN_SIZE: %dx%d", m_w, m_h);
      return;
    }
  }

  LOG_WARN("uinput: could not detect screen size, using default %dx%d", m_w, m_h);
}

void UInputScreen::initUInput()
{
  // Create keyboard uinput device
  m_uinputKeyboard = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (m_uinputKeyboard < 0) {
    LOG_ERR("uinput: failed to open /dev/uinput for keyboard: %s", strerror(errno));
    throw std::runtime_error("failed to open /dev/uinput - ensure the uinput module is loaded and you have permission");
  }

  // Enable key events
  ioctl(m_uinputKeyboard, UI_SET_EVBIT, EV_KEY);
  // Enable all standard keys (0..KEY_MAX)
  for (int i = 0; i < KEY_MAX; i++) {
    ioctl(m_uinputKeyboard, UI_SET_KEYBIT, i);
  }

  // Enable repeat events
  ioctl(m_uinputKeyboard, UI_SET_EVBIT, EV_REP);

  struct uinput_setup kbdSetup = {};
  std::strncpy(kbdSetup.name, "deskflow-keyboard", UINPUT_MAX_NAME_SIZE - 1);
  kbdSetup.id.bustype = BUS_VIRTUAL;
  kbdSetup.id.vendor = 0x1234;
  kbdSetup.id.product = 0x5678;
  kbdSetup.id.version = 1;

  if (ioctl(m_uinputKeyboard, UI_DEV_SETUP, &kbdSetup) < 0) {
    LOG_ERR("uinput: keyboard UI_DEV_SETUP failed: %s", strerror(errno));
    close(m_uinputKeyboard);
    m_uinputKeyboard = -1;
    throw std::runtime_error("failed to setup uinput keyboard device");
  }

  if (ioctl(m_uinputKeyboard, UI_DEV_CREATE) < 0) {
    LOG_ERR("uinput: keyboard UI_DEV_CREATE failed: %s", strerror(errno));
    close(m_uinputKeyboard);
    m_uinputKeyboard = -1;
    throw std::runtime_error("failed to create uinput keyboard device");
  }

  LOG_INFO("uinput: keyboard device created");

  // Create relative mouse uinput device
  m_uinputMouse = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (m_uinputMouse < 0) {
    LOG_ERR("uinput: failed to open /dev/uinput for mouse: %s", strerror(errno));
    throw std::runtime_error("failed to open /dev/uinput for mouse");
  }

  ioctl(m_uinputMouse, UI_SET_EVBIT, EV_KEY);
  ioctl(m_uinputMouse, UI_SET_KEYBIT, BTN_LEFT);
  ioctl(m_uinputMouse, UI_SET_KEYBIT, BTN_RIGHT);
  ioctl(m_uinputMouse, UI_SET_KEYBIT, BTN_MIDDLE);
  ioctl(m_uinputMouse, UI_SET_KEYBIT, BTN_SIDE);
  ioctl(m_uinputMouse, UI_SET_KEYBIT, BTN_EXTRA);

  ioctl(m_uinputMouse, UI_SET_EVBIT, EV_REL);
  ioctl(m_uinputMouse, UI_SET_RELBIT, REL_X);
  ioctl(m_uinputMouse, UI_SET_RELBIT, REL_Y);
  ioctl(m_uinputMouse, UI_SET_RELBIT, REL_WHEEL);
  ioctl(m_uinputMouse, UI_SET_RELBIT, REL_HWHEEL);
  ioctl(m_uinputMouse, UI_SET_RELBIT, REL_WHEEL_HI_RES);
  ioctl(m_uinputMouse, UI_SET_RELBIT, REL_HWHEEL_HI_RES);

  struct uinput_setup mouseSetup = {};
  std::strncpy(mouseSetup.name, "deskflow-mouse", UINPUT_MAX_NAME_SIZE - 1);
  mouseSetup.id.bustype = BUS_VIRTUAL;
  mouseSetup.id.vendor = 0x1234;
  mouseSetup.id.product = 0x5679;
  mouseSetup.id.version = 1;

  if (ioctl(m_uinputMouse, UI_DEV_SETUP, &mouseSetup) < 0) {
    LOG_ERR("uinput: mouse UI_DEV_SETUP failed: %s", strerror(errno));
    close(m_uinputMouse);
    m_uinputMouse = -1;
    throw std::runtime_error("failed to setup uinput mouse device");
  }

  if (ioctl(m_uinputMouse, UI_DEV_CREATE) < 0) {
    LOG_ERR("uinput: mouse UI_DEV_CREATE failed: %s", strerror(errno));
    close(m_uinputMouse);
    m_uinputMouse = -1;
    throw std::runtime_error("failed to create uinput mouse device");
  }

  LOG_INFO("uinput: relative mouse device created");

  // Create absolute mouse uinput device (for fakeMouseMove with absolute coords)
  m_uinputAbsMouse = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (m_uinputAbsMouse < 0) {
    LOG_ERR("uinput: failed to open /dev/uinput for abs mouse: %s", strerror(errno));
    throw std::runtime_error("failed to open /dev/uinput for abs mouse");
  }

  ioctl(m_uinputAbsMouse, UI_SET_EVBIT, EV_ABS);
  ioctl(m_uinputAbsMouse, UI_SET_ABSBIT, ABS_X);
  ioctl(m_uinputAbsMouse, UI_SET_ABSBIT, ABS_Y);

  ioctl(m_uinputAbsMouse, UI_SET_EVBIT, EV_KEY);
  ioctl(m_uinputAbsMouse, UI_SET_KEYBIT, BTN_LEFT);
  ioctl(m_uinputAbsMouse, UI_SET_KEYBIT, BTN_RIGHT);
  ioctl(m_uinputAbsMouse, UI_SET_KEYBIT, BTN_MIDDLE);

  // Set up absolute axis ranges
  struct uinput_abs_setup absSetupX = {};
  absSetupX.code = ABS_X;
  absSetupX.absinfo.minimum = 0;
  absSetupX.absinfo.maximum = m_w > 0 ? m_w - 1 : 1919;
  absSetupX.absinfo.resolution = 1;

  struct uinput_abs_setup absSetupY = {};
  absSetupY.code = ABS_Y;
  absSetupY.absinfo.minimum = 0;
  absSetupY.absinfo.maximum = m_h > 0 ? m_h - 1 : 1079;
  absSetupY.absinfo.resolution = 1;

  ioctl(m_uinputAbsMouse, UI_ABS_SETUP, &absSetupX);
  ioctl(m_uinputAbsMouse, UI_ABS_SETUP, &absSetupY);

  struct uinput_setup absMouseSetup = {};
  std::strncpy(absMouseSetup.name, "deskflow-abs-mouse", UINPUT_MAX_NAME_SIZE - 1);
  absMouseSetup.id.bustype = BUS_VIRTUAL;
  absMouseSetup.id.vendor = 0x1234;
  absMouseSetup.id.product = 0x567A;
  absMouseSetup.id.version = 1;

  if (ioctl(m_uinputAbsMouse, UI_DEV_SETUP, &absMouseSetup) < 0) {
    LOG_ERR("uinput: abs mouse UI_DEV_SETUP failed: %s", strerror(errno));
    close(m_uinputAbsMouse);
    m_uinputAbsMouse = -1;
    throw std::runtime_error("failed to setup uinput abs mouse device");
  }

  if (ioctl(m_uinputAbsMouse, UI_DEV_CREATE) < 0) {
    LOG_ERR("uinput: abs mouse UI_DEV_CREATE failed: %s", strerror(errno));
    close(m_uinputAbsMouse);
    m_uinputAbsMouse = -1;
    throw std::runtime_error("failed to create uinput abs mouse device");
  }

  LOG_INFO("uinput: absolute mouse device created (range: %dx%d)", m_w, m_h);
}

void UInputScreen::cleanupUInput()
{
  if (m_uinputKeyboard >= 0) {
    ioctl(m_uinputKeyboard, UI_DEV_DESTROY);
    close(m_uinputKeyboard);
    m_uinputKeyboard = -1;
    LOG_DEBUG("uinput: keyboard device destroyed");
  }

  if (m_uinputMouse >= 0) {
    ioctl(m_uinputMouse, UI_DEV_DESTROY);
    close(m_uinputMouse);
    m_uinputMouse = -1;
    LOG_DEBUG("uinput: mouse device destroyed");
  }

  if (m_uinputAbsMouse >= 0) {
    ioctl(m_uinputAbsMouse, UI_DEV_DESTROY);
    close(m_uinputAbsMouse);
    m_uinputAbsMouse = -1;
    LOG_DEBUG("uinput: abs mouse device destroyed");
  }
}

void UInputScreen::emitEvent(int fd, int type, int code, int value) const
{
  struct input_event ev = {};
  ev.type = type;
  ev.code = code;
  ev.value = value;

  if (write(fd, &ev, sizeof(ev)) < 0) {
    LOG_ERR("uinput: write failed: %s", strerror(errno));
  }
}

void UInputScreen::syncEvent(int fd) const
{
  emitEvent(fd, EV_SYN, SYN_REPORT, 0);
}

void *UInputScreen::getEventTarget() const
{
  return const_cast<void *>(static_cast<const void *>(this));
}

bool UInputScreen::getClipboard(ClipboardID, IClipboard *) const
{
  // Clipboard not supported in headless uinput mode
  return false;
}

void UInputScreen::getShape(std::int32_t &x, std::int32_t &y, std::int32_t &w, std::int32_t &h) const
{
  x = m_x;
  y = m_y;
  w = m_w;
  h = m_h;
}

void UInputScreen::getCursorPos(std::int32_t &x, std::int32_t &y) const
{
  x = m_cursorX;
  y = m_cursorY;
}

void UInputScreen::reconfigure(std::uint32_t activeSides)
{
  const static auto sidesText = sidesMaskToString(activeSides);
  LOG_DEBUG("active sides: %s (0x%02x)", sidesText.c_str(), activeSides);
  m_activeSides = activeSides;
}

std::uint32_t UInputScreen::activeSides()
{
  return m_activeSides;
}

void UInputScreen::warpCursor(std::int32_t x, std::int32_t y)
{
  m_cursorX = x;
  m_cursorY = y;
}

std::uint32_t UInputScreen::registerHotKey(KeyID, KeyModifierMask)
{
  // Hotkeys not supported in uinput mode
  return 0;
}

void UInputScreen::unregisterHotKey(std::uint32_t)
{
  // Hotkeys not supported in uinput mode
}

void UInputScreen::fakeInputBegin()
{
  // Nothing to do
}

void UInputScreen::fakeInputEnd()
{
  // Nothing to do
}

std::int32_t UInputScreen::getJumpZoneSize() const
{
  return 1;
}

bool UInputScreen::isAnyMouseButtonDown(std::uint32_t &) const
{
  return false;
}

void UInputScreen::getCursorCenter(std::int32_t &x, std::int32_t &y) const
{
  x = m_x + m_w / 2;
  y = m_y + m_h / 2;
}

void UInputScreen::fakeMouseButton(ButtonID button, bool press)
{
  if (m_uinputMouse < 0)
    return;

  int code;
  switch (button) {
  case kButtonLeft:
    code = BTN_LEFT;
    break;
  case kButtonMiddle:
    code = BTN_MIDDLE;
    break;
  case kButtonRight:
    code = BTN_RIGHT;
    break;
  case kButtonExtra0:
    code = BTN_SIDE;
    break;
  case kButtonExtra1:
    code = BTN_EXTRA;
    break;
  default:
    code = BTN_LEFT + (button - 1);
    break;
  }

  emitEvent(m_uinputMouse, EV_KEY, code, press ? 1 : 0);
  syncEvent(m_uinputMouse);

  LOG_DEBUG1("uinput: mouse button %d %s", button, press ? "press" : "release");
}

void UInputScreen::fakeMouseMove(std::int32_t x, std::int32_t y)
{
  // We get one motion event before enter() with the target position
  if (!m_isOnScreen) {
    m_cursorX = x;
    m_cursorY = y;
    return;
  }

  if (m_uinputAbsMouse < 0)
    return;

  emitEvent(m_uinputAbsMouse, EV_ABS, ABS_X, x);
  emitEvent(m_uinputAbsMouse, EV_ABS, ABS_Y, y);
  syncEvent(m_uinputAbsMouse);

  m_cursorX = x;
  m_cursorY = y;

  LOG_DEBUG2("uinput: abs mouse move to %d,%d", x, y);
}

void UInputScreen::fakeMouseRelativeMove(std::int32_t dx, std::int32_t dy) const
{
  if (m_uinputMouse < 0)
    return;

  emitEvent(m_uinputMouse, EV_REL, REL_X, dx);
  emitEvent(m_uinputMouse, EV_REL, REL_Y, dy);
  syncEvent(m_uinputMouse);

  LOG_DEBUG2("uinput: rel mouse move %d,%d", dx, dy);
}

void UInputScreen::fakeMouseWheel(ScrollDelta delta) const
{
  if (m_uinputMouse < 0)
    return;

  delta = applyScrollModifier(delta);

  // Linux kernel uses opposite sign convention for scroll direction vs deskflow.
  // Also, kernel REL_WHEEL expects discrete clicks (multiples of 1), while
  // deskflow uses multiples of 120. We also send hi-res events.
  if (delta.y != 0) {
    // Hi-res wheel events (120 units = one click)
    emitEvent(m_uinputMouse, EV_REL, REL_WHEEL_HI_RES, -delta.y);
    // Traditional wheel event (1 unit = one click)
    int clicks = -delta.y / s_scrollDelta;
    if (clicks != 0) {
      emitEvent(m_uinputMouse, EV_REL, REL_WHEEL, clicks);
    }
  }

  if (delta.x != 0) {
    emitEvent(m_uinputMouse, EV_REL, REL_HWHEEL_HI_RES, -delta.x);
    int clicks = -delta.x / s_scrollDelta;
    if (clicks != 0) {
      emitEvent(m_uinputMouse, EV_REL, REL_HWHEEL, clicks);
    }
  }

  syncEvent(m_uinputMouse);
}

void UInputScreen::fakeKey(std::uint32_t keycode, bool isDown) const
{
  if (m_uinputKeyboard < 0)
    return;

  // keycode is an evdev keycode (Linux input.h code)
  emitEvent(m_uinputKeyboard, EV_KEY, keycode, isDown ? 1 : 0);
  syncEvent(m_uinputKeyboard);

  // Also update xkb state tracking
  auto xkbKeycode = keycode + 8;
  m_keyState->updateXkbState(xkbKeycode, isDown);

  LOG_DEBUG1("uinput: key %d %s", keycode, isDown ? "down" : "up");
}

void UInputScreen::enable()
{
  // Nothing to do for uinput
}

void UInputScreen::disable()
{
  // Nothing to do for uinput
}

void UInputScreen::enter()
{
  m_isOnScreen = true;
  ++m_sequenceNumber;

  // Move cursor to the target position on enter
  if (m_uinputAbsMouse >= 0) {
    fakeMouseMove(m_cursorX, m_cursorY);
  }
}

bool UInputScreen::canLeave()
{
  return true;
}

void UInputScreen::leave()
{
  m_isOnScreen = false;
}

bool UInputScreen::setClipboard(ClipboardID, const IClipboard *)
{
  // Clipboard not supported in headless uinput mode
  return false;
}

void UInputScreen::checkClipboards()
{
  // No clipboard support
}

void UInputScreen::openScreensaver(bool)
{
  // No screensaver support
}

void UInputScreen::closeScreensaver()
{
  // No screensaver support
}

void UInputScreen::screensaver(bool)
{
  // No screensaver support
}

void UInputScreen::resetOptions()
{
  // No uinput-specific options
}

void UInputScreen::setOptions(const OptionsList &)
{
  // No uinput-specific options
}

bool UInputScreen::isPrimary() const
{
  return m_isPrimary;
}

void UInputScreen::setSequenceNumber(std::uint32_t seqNum)
{
  m_sequenceNumber = seqNum;
}

void UInputScreen::handleSystemEvent(const Event &)
{
  // The uinput screen doesn't receive external events (it only sends).
  // This handler exists to satisfy the interface.
}

void UInputScreen::updateButtons()
{
  // Nothing to do - we don't track button state
}

IKeyState *UInputScreen::getKeyState() const
{
  return m_keyState;
}

std::string UInputScreen::getSecureInputApp() const
{
  return "";
}

void UInputScreen::sendEvent(EventTypes type, void *data)
{
  m_events->addEvent(Event(type, getEventTarget(), data));
}

} // namespace deskflow
