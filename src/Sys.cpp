/*
 * Sys.cpp
 *
 *  Created on: May 15, 2016
 *      Author: lieven
 */

#include <Log.h>
#include <Sys.h>
#include <stdint.h>
#include <sys/time.h>
//#include <espressif/esp_wifi.h>

uint64_t Sys::_upTime;

#include <time.h>
#include <unistd.h>

const char* Sys::cpu() {
  static char* _cpu = 0;
  if (_cpu != 0) return _cpu;
  FILE* cpuinfo = fopen("/proc/cpuinfo", "rb");
  char* arg = 0;
  size_t size = 0;
  while (getdelim(&arg, &size, 0, cpuinfo) != -1) {
    std::string line = arg;
    if (line.find("model name") >= 0) {
      int keyOffset = line.find("model name");
      int startValue = line.find(":", keyOffset);
      int endValue = line.find("\n", keyOffset);
      std::string cpu =
          line.substr(startValue + 1, (endValue - startValue) - 1);
      _cpu = (char*)malloc(cpu.length() + 1);
      strcpy(_cpu, cpu.c_str());
    }
  }
  free(arg);
  fclose(cpuinfo);
  return _cpu;
}

uint64_t Sys::millis()  // time in msec since boot, only increasing
{
  struct timespec deadline;
  clock_gettime((int)CLOCK_MONOTONIC, &deadline);
  Sys::_upTime = deadline.tv_sec * 1000 + deadline.tv_nsec / 1000000;
  return _upTime;
}

void Sys::init() { gethostname(_hostname, 30); }

void Sys::delay(uint32_t time) { usleep(time * 1000); }

uint64_t Sys::now() { return _boot_time + Sys::millis(); }

void Sys::setNow(uint64_t n) { _boot_time = n - Sys::millis(); }

void Sys::hostname(const char* hostname) {
  strncpy(_hostname, hostname, sizeof(_hostname));
}

const char* Sys::hostname() {
  if (strlen(_hostname) == 0) gethostname(_hostname, 30);
  return _hostname;
}

uint32_t Sys::getSerialId() { return 0xDEADBEEF; }

const char* Sys::getProcessor() { return "AMD64"; }
const char* Sys::getBuild() { return __DATE__ " " __TIME__; }

uint32_t Sys::getFreeHeap() { return 123456789; };

char Sys::_hostname[30];
uint64_t Sys::_boot_time = 0;

/*
uint32_t Sys::sec()
{
        return millis()/1000;
}
*/

void Sys::setHostname(const char* h) { strncpy(_hostname, h, strlen(h) + 1); }

extern "C" uint64_t SysMillis() { return Sys::millis(); }
