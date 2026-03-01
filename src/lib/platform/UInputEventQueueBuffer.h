/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/IEventQueueBuffer.h"

#include <mutex>
#include <queue>

class IEventQueue;

namespace deskflow {

//! Event queue buffer for uinput (headless) mode
/*!
 * A simple pipe-based event queue buffer for the uinput screen.
 * Since uinput doesn't generate events (it only sends), this
 * is a minimal implementation that handles user-injected events
 * and system timer events via a self-pipe.
 */
class UInputEventQueueBuffer : public IEventQueueBuffer
{
public:
  explicit UInputEventQueueBuffer(IEventQueue *events);
  ~UInputEventQueueBuffer() override;

  // IEventQueueBuffer overrides
  void init() override;
  void waitForEvent(double msTimeout) override;
  Type getEvent(Event &event, uint32_t &dataID) override;
  bool addEvent(uint32_t dataID) override;
  bool isEmpty() const override;

private:
  IEventQueue *m_events;
  std::queue<uint32_t> m_queue;
  int m_pipeWrite;
  int m_pipeRead;

  mutable std::mutex m_mutex;
};

} // namespace deskflow
