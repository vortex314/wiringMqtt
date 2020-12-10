#include "NanoAkka.h"

#include <asm-generic/ioctls.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>

NanoStats stats;
/*
 _____ _                        _
|_   _| |__  _ __ ___  __ _  __| |
  | | | '_ \| '__/ _ \/ _` |/ _` |
  | | | | | | | |  __/ (_| | (_| |
  |_| |_| |_|_|  \___|\__,_|\__,_|
*/
int Thread::_id = 0;

int readPipe(int readFd, void* data, int size, uint32_t timeout) {
  fd_set rfds;
  fd_set wfds;
  fd_set efds;
  struct timeval tv;

  tv.tv_sec = timeout / 1000;
  tv.tv_usec = (timeout * 1000) % 1000000;

  // Watch serialFd and tcpFd  to see when it has input.
  FD_ZERO(&rfds);
  FD_ZERO(&wfds);
  FD_ZERO(&efds);

  FD_SET(readFd, &rfds);
  FD_SET(readFd, &efds);
  int maxFd = readFd;
  maxFd += 1;

  int rc = select(maxFd, &rfds, 0, &efds, &tv);

  if (rc < 0) {
    WARN(" select() : error : %s (%d)", strerror(errno), errno);
    return rc;
  } else if (rc > 0) {  // one of the fd was set

    if (FD_ISSET(readFd, &rfds)) {
      ::read(readFd, data, size);  // read 1 event
      return 1;
    }

    if (FD_ISSET(readFd, &efds)) {
      WARN("pipe  error : %s (%d)", strerror(errno), errno);
      return ECOMM;
    }
  } else {
    TRACE(" timeout %llu", Sys::millis());
    return ETIMEDOUT;
  }
  return ECOMM;
}

void Thread::createQueue() {
  int rc = pipe(_pipeFd);
  _writePipe = _pipeFd[1];
  _readPipe = _pipeFd[0];
  if (rc < 0) WARN("Queue creation failed %d %s ", errno, strerror(errno));
  if (fcntl(_writePipe, F_SETFL, O_NONBLOCK) < 0) {
    WARN("Failed to set pipe blocking mode: %s (%d)", strerror(errno), errno);
  }
}

void SetThreadName(std::thread* thread, const char* threadName) {
  auto handle = thread->native_handle();
  pthread_setname_np(handle, threadName);
}

void Thread::start() {
  INFO("");
  std::thread thr = std::thread([=]() { run(); });
  SetThreadName(&thr, _name.c_str());
}

int Thread::enqueue(Invoker* invoker) {
  //  INFO("Thread '%s' >>> '%lX", _name.c_str(), invoker);
  if (_writePipe)
    if (write(_writePipe, (const char*)&invoker, sizeof(Invoker*)) == -1) {
      stats.threadQueueOverflow++;
      WARN("Thread '%s' queue overflow [%X]", _name.c_str(), invoker);
      return ENOBUFS;
    }
  return 0;
};
int Thread::enqueueFromIsr(Invoker* invoker) { return enqueue(invoker); };

void Thread::run() {
  INFO("Thread '%s' started ", _name.c_str());
  uint32_t noWaits = 0;
  while (true) {
    uint64_t now = Sys::millis();
    uint64_t expTime = now + 5000;
    TimerSource* expiredTimer = 0;
    // find next expired timer if any within 5 sec
    for (auto timer : _timers) {
      if (timer->expireTime() < expTime) {
        expTime = timer->expireTime();
        expiredTimer = timer;
      }
    }
    int32_t waitTime =
        (expTime - now);  // ESP_OPEN_RTOS seems to double sleep time ?

    //		INFO(" waitTime : %d ",waitTime);
    if (noWaits % 1000 == 999)
      WARN(" noWaits : %d in thread %s waitTime %d ", noWaits, _name.c_str(),
           waitTime);
    if (waitTime > 0) {
      Invoker* prq = 0;
      if (waitTime == 0) noWaits++;
      unsigned int priority = 0;
      DEBUG(" waiting ... %d msec", waitTime);
      int rc = readPipe(_readPipe, &prq, sizeof(Invoker*), waitTime);
      if (rc == 1) {
        DEBUG(" got message %d on queue %lX", rc, prq);
        uint64_t start = Sys::millis();
        prq->invoke();
        DEBUG(" returned invoke");
        uint32_t delta = Sys::millis() - start;
        if (delta > 50)
          WARN("Invoker [%X] slow %d msec invoker on thread '%s'.", prq, delta,
               _name.c_str());
      } else {
        //        WARN(" readPipe : %d  : error : %s (%d)", rc, strerror(errno),
        //        errno);
        noWaits = 0;
      }
    } else {
      noWaits++;
      if (expiredTimer) {
        if (-waitTime > 100)
          INFO("Timer[%X] already expired by %u msec on thread '%s'.",
               expiredTimer, -waitTime, _name.c_str());
        uint64_t start = Sys::millis();
        expiredTimer->request();
        uint32_t deltaExec = Sys::millis() - start;
        if (deltaExec > 50)
          WARN("Timer [%X] request slow %d msec on thread '%s'", expiredTimer,
               deltaExec, _name.c_str());
      }
    }
  }
}
