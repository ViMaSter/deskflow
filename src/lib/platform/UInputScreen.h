/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/PlatformScreen.h"

#include <map>
#include <mutex>
#include <vector>

class IEventQueue;

namespace deskflow {

class UInputKeyState;

//! Implementation of IPlatformScreen using Linux uinput
/*!
 * This screen implementation uses the Linux kernel's uinput subsystem
 * to synthesize keyboard/mouse input at the kernel level. This allows
 * it to work without any display server (X11/Wayland), making it suitable
 * for use as a system service that runs before user login.
 *
 * Only the secondary (client) role is fully supported since uinput
 * can only inject input, not capture it.
 */
class UInputScreen : public PlatformScreen
{
public:
  UInputScreen(bool isPrimary, IEventQueue *events);
  ~UInputScreen() override;

  // IScreen overrides
  void *getEventTarget() const final;
  bool getClipboard(ClipboardID id, IClipboard *) const override;
  void getShape(std::int32_t &x, std::int32_t &y, std::int32_t &width, std::int32_t &height) const override;
  void getCursorPos(std::int32_t &x, std::int32_t &y) const override;

  // IPrimaryScreen overrides
  void reconfigure(std::uint32_t activeSides) override;
  std::uint32_t activeSides() override;
  void warpCursor(std::int32_t x, std::int32_t y) override;
  std::uint32_t registerHotKey(KeyID key, KeyModifierMask mask) override;
  void unregisterHotKey(std::uint32_t id) override;
  void fakeInputBegin() override;
  void fakeInputEnd() override;
  std::int32_t getJumpZoneSize() const override;
  bool isAnyMouseButtonDown(std::uint32_t &buttonID) const override;
  void getCursorCenter(std::int32_t &x, std::int32_t &y) const override;

  // ISecondaryScreen overrides
  void fakeMouseButton(ButtonID id, bool press) override;
  void fakeMouseMove(std::int32_t x, std::int32_t y) override;
  void fakeMouseRelativeMove(std::int32_t dx, std::int32_t dy) const override;
  void fakeMouseWheel(ScrollDelta delta) const override;
  void fakeKey(std::uint32_t keycode, bool isDown) const;

  // IPlatformScreen overrides
  void enable() override;
  void disable() override;
  void enter() override;
  bool canLeave() override;
  void leave() override;
  bool setClipboard(ClipboardID, const IClipboard *) override;
  void checkClipboards() override;
  void openScreensaver(bool notify) override;
  void closeScreensaver() override;
  void screensaver(bool activate) override;
  void resetOptions() override;
  void setOptions(const OptionsList &options) override;
  void setSequenceNumber(std::uint32_t) override;
  bool isPrimary() const override;

protected:
  // IPlatformScreen overrides
  void handleSystemEvent(const Event &event) override;
  void updateButtons() override;
  IKeyState *getKeyState() const override;
  std::string getSecureInputApp() const override;

private:
  void initUInput();
  void cleanupUInput();
  void sendEvent(EventTypes type, void *data);
  void emitEvent(int fd, int type, int code, int value) const;
  void syncEvent(int fd) const;

  /// Reads the current framebuffer resolution from /sys/class/graphics/fb0
  void detectScreenSize();

  bool m_isPrimary = false;
  IEventQueue *m_events = nullptr;

  UInputKeyState *m_keyState = nullptr;

  int m_uinputKeyboard = -1;
  int m_uinputMouse = -1;
  int m_uinputAbsMouse = -1;

  std::uint32_t m_sequenceNumber = 0;
  std::uint32_t m_activeSides = 0;

  std::int32_t m_x = 0;
  std::int32_t m_y = 0;
  std::int32_t m_w = 1920;
  std::int32_t m_h = 1080;

  std::int32_t m_cursorX = 0;
  std::int32_t m_cursorY = 0;

  bool m_isOnScreen = false;

  mutable std::mutex m_mutex;
};

} // namespace deskflow
