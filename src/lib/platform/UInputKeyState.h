/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/KeyState.h"

#include <xkbcommon/xkbcommon.h>

struct xkb_context;
struct xkb_keymap;
struct xkb_state;

namespace deskflow {

class UInputScreen;

/// A key state for uinput-based input using xkbcommon for keymap handling
class UInputKeyState : public KeyState
{
public:
  UInputKeyState(UInputScreen *screen, IEventQueue *events);
  ~UInputKeyState() override;

  void initDefaultKeymap();

  // IKeyState overrides
  bool fakeCtrlAltDel() override;
  KeyModifierMask pollActiveModifiers() const override;
  std::int32_t pollActiveGroup() const override;
  void pollPressedKeys(KeyButtonSet &pressedKeys) const override;
  void updateXkbState(std::uint32_t keyval, bool isPressed);
  void clearStaleModifiers() override;

protected:
  // KeyState overrides
  void getKeyMap(KeyMap &keyMap) override;
  void fakeKey(const Keystroke &keystroke) override;

private:
  std::uint32_t convertModMask(xkb_mod_mask_t xkbModMaskIn) const;
  void assignGeneratedModifiers(std::uint32_t keycode, KeyMap::KeyItem &item);

  UInputScreen *m_screen = nullptr;

  xkb_context *m_xkb = nullptr;
  xkb_keymap *m_xkbKeymap = nullptr;
  xkb_state *m_xkbState = nullptr;
};

} // namespace deskflow
