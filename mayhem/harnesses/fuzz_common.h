// Shared setup for the speeduino libFuzzer harnesses. They run the firmware on upstream's own
// native fake board (lib/ArduinoFake/FakeMega + ArduinoNativeFake), exactly as test/ does, with two
// environment mocks so a run depends only on its input:
//  * a deterministic clock: millis() advances 1 ms and micros() 50 us on every call instead of reading the
//    wall clock, so the firmware's own 400 ms serial receive timeout expires after a bounded number of
//    polls instead of after real time, and delay() does not sleep;
//  * a RAM-backed storage API (the firmware's own setStorageAPI hook, as test/test_storage does), reset
//    for every input.
#pragma once
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <sstream>
#include <single_header/standalone/fakeit.hpp>
#include <SimpleArduinoFake.h>
#include "FakeMega.h"
#include "globals.h"
#include "comms.h"
#include "comms_legacy.h"
#include "pages.h"
#include "storage.h"
#include "storage_api.h"
#include "sensors.h"

// Defined in mayhem/harnesses/comms{,_legacy}_fuzz.cpp: reset the file-static protocol state of
// upstream's comms.cpp / comms_legacy.cpp, which the fuzz build compiles through those wrappers.
void fuzz_reset_comms(void);
void fuzz_reset_legacy(void);
extern uint8_t currentSecondaryCommand;

namespace fuzzenv {

static uint64_t g_micros = 0;
static std::stringstream g_serialIn, g_serialOut;
static uint8_t g_storage[4096];

static byte st_read(uint16_t a) { return g_storage[a % sizeof(g_storage)]; }
static void st_write(uint16_t a, byte v) { g_storage[a % sizeof(g_storage)] = v; }
static uint16_t st_length(void) { return (uint16_t)sizeof(g_storage); }
static uint16_t st_block(const statuses &) { return 64U; }

static inline void initOnce(void) {
  static bool done = false;
  if (done) return;
  done = true;
  fakeMega(g_serialOut, g_serialIn);
  auto &fn = SimpleArduinoFake::getContext()._Function;
  fakeit::When(Method(fn, millis)).AlwaysDo([]() -> unsigned long { g_micros += 1000U; return (unsigned long)(g_micros / 1000U); });
  fakeit::When(Method(fn, micros)).AlwaysDo([]() -> unsigned long { g_micros += 50U; return (unsigned long)g_micros; });
  fakeit::When(Method(fn, delay)).AlwaysDo([](unsigned long ms) { g_micros += (uint64_t)ms * 1000U; });
  fakeit::When(Method(fn, delayMicroseconds)).AlwaysDo([](unsigned long us) { g_micros += us; });
  storage_api_t api = { st_read, st_write, st_length, st_block };
  setStorageAPI(api);
}

// Reset all firmware state an input can change: status, every config page/table, storage, serial flags,
// the comms/legacy-comms file statics and the tooth/composite log buffers.
static inline void resetState(uint8_t storageFill) {
  initOnce();
  // fakeit records every mocked call for Verify(); drop that history so it does not grow per input.
  auto &ctx = SimpleArduinoFake::getContext();
  ctx._Function.ClearInvocationHistory(); ctx._Serial.ClearInvocationHistory();
  ctx._Wire.ClearInvocationHistory();     ctx._Stream.ClearInvocationHistory();
  ctx._Client.ClearInvocationHistory();   ctx._Print.ClearInvocationHistory();
  ctx._SPI.ClearInvocationHistory();      ctx._EEPROM.ClearInvocationHistory();
  g_micros += 10U * 1000U * 1000U;        // start well clear of any previous deadline
  memset(g_storage, storageFill, sizeof(g_storage));
  currentStatus = statuses();
  memset(&configPage2, 0, sizeof(configPage2));
  memset(&configPage4, 0, sizeof(configPage4));
  memset(&configPage6, 0, sizeof(configPage6));
  memset(&configPage9, 0, sizeof(configPage9));
  memset(&configPage10, 0, sizeof(configPage10));
  memset(&configPage13, 0, sizeof(configPage13));
  memset(&configPage15, 0, sizeof(configPage15));
  setTuneToEmpty();
  fuzz_reset_comms();
  fuzz_reset_legacy();
  currentSecondaryCommand = 0;
  // Sensor calibration tables (sensors.cpp) are written by the TunerStudio/legacy calibration
  // commands and read back by legacy 'Z' and the table2D lookups: clear values, axis and cache.
  for (table2D_u16_u8_32 *t : {&cltCalibrationTable, &iatCalibrationTable, &o2CalibrationTable}) {
    memset(t->values, 0, sizeof(t->values));
    memset(t->axis, 0, sizeof(t->axis));
    t->cache = decltype(t->cache)();
  }
  memset((void *)toothHistory, 0, sizeof(toothHistory));
  memset((void *)compositeLogHistory, 0, sizeof(compositeLogHistory));
  toothHistoryIndex = 0;
  serialStatusFlag = SERIAL_INACTIVE;
  serialSecondaryStatusFlag = SERIAL_INACTIVE;
  g_serialIn.str(std::string()); g_serialIn.clear();
  g_serialOut.str(std::string()); g_serialOut.clear();
}

// A serial port over the fuzz input (receive side); transmitted bytes are counted and dropped. Used
// instead of the fakeit-mocked Serial, which records every call and would grow without bound.
template <class Base>
class BufferPort : public Base {
public:
  const uint8_t *data = nullptr; size_t size = 0, pos = 0, written = 0;
  void reset(const uint8_t *d, size_t n) { data = d; size = n; pos = 0; written = 0; }
  int available(void) override { return (int)(size - pos); }
  int availableForWrite(void) override { return 1024; }
  int read(void) override { return pos < size ? data[pos++] : -1; }
  int peek(void) override { return pos < size ? data[pos] : -1; }
  void flush(void) override {}
  size_t write(uint8_t) override { ++written; return 1; }
  size_t write(const uint8_t *, size_t n) override { written += n; return n; }
};
using BufferStream = BufferPort<Stream>;

} // namespace fuzzenv
