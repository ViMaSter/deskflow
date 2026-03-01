/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/UInputEventQueueBuffer.h"

#include "base/Event.h"
#include "base/EventTypes.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "mt/Thread.h"

#include <cassert>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace deskflow {

UInputEventQueueBuffer::UInputEventQueueBuffer(IEventQueue *events) : m_events{events}
{
  int pipefd[2];
  int result = pipe2(pipefd, O_NONBLOCK);
  assert(result == 0);
  (void)result;

  m_pipeRead = pipefd[0];
  m_pipeWrite = pipefd[1];
}

UInputEventQueueBuffer::~UInputEventQueueBuffer()
{
  close(m_pipeRead);
  close(m_pipeWrite);
}

void UInputEventQueueBuffer::init()
{
  // Nothing to initialize
}

void UInputEventQueueBuffer::waitForEvent(double msTimeout)
{
  Thread::testCancel();

  struct pollfd pfd;
  pfd.fd = m_pipeRead;
  pfd.events = POLLIN;

  int timeout = (msTimeout < 0.0) ? -1 : static_cast<int>(1000.0 * msTimeout);

  if (int retval = poll(&pfd, 1, timeout); retval > 0) {
    if (pfd.revents & POLLIN) {
      char buf[64];
      while (read(m_pipeRead, buf, sizeof(buf)) > 0) {
        // drain the pipe
      }
    }
  }

  Thread::testCancel();
}

IEventQueueBuffer::Type UInputEventQueueBuffer::getEvent(Event &event, uint32_t &dataID)
{
  std::scoped_lock lock{m_mutex};

  if (m_queue.empty()) {
    return IEventQueueBuffer::Type::Unknown;
  }

  dataID = m_queue.front();
  m_queue.pop();

  return IEventQueueBuffer::Type::User;
}

bool UInputEventQueueBuffer::addEvent(uint32_t dataID)
{
  std::scoped_lock lock{m_mutex};
  m_queue.push(dataID);

  // Wake up the poll
  auto result = write(m_pipeWrite, "!", 1);
  LOG_DEBUG2("uinput event queue write result: %zd", result);

  return true;
}

bool UInputEventQueueBuffer::isEmpty() const
{
  std::scoped_lock lock{m_mutex};
  return m_queue.empty();
}

} // namespace deskflow
