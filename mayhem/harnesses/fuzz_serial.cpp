// Primary serial (TunerStudio) protocol: serialReceive() + serialTransmit(), the firmware's main-loop
// comms pump (speeduino.ino loop()). Reaches processSerialCommand (new CRC-framed protocol: page
// read/write 'p'/'M', output channels 'r', tooth + composite logs 'T'/'t', calibration, burn) and
// legacySerialCommand (single-letter legacy protocol).
//
// Input: byte 0 selects the mode.
//   bit0 = 1: the rest is fed raw to the serial port with legacy comms enabled (as after power-on).
//   bit0 = 0: the rest is a list of records, each [flags:u8][len:u16 BE][len bytes payload], which the
//             harness frames the way TunerStudio does: u16 BE length + payload + CRC32 (BE), the CRC
//             correct unless flags bit0 is set; bit1 of byte 0 keeps legacy comms enabled.
#include "fuzz_common.h"   // first: fakeit must precede Arduino.h's abs/min/max macros
#include <vector>
#include <FastCRC.h>

static fuzzenv::BufferStream g_stream;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 1) return -1;
  fuzzenv::resetState(0xFF);
  const uint8_t mode = data[0];
  std::vector<uint8_t> wire;
  if (mode & 1U) {
    wire.assign(data + 1, data + size);
    currentStatus.allowLegacyComms = true;
  } else {
    currentStatus.allowLegacyComms = (mode & 2U) != 0;
    FastCRC32 crc;
    size_t i = 1;
    while (i + 3 <= size) {
      uint8_t flags = data[i];
      size_t len = ((size_t)data[i + 1] << 8) | data[i + 2];
      i += 3;
      if (len > size - i) len = size - i;
      uint32_t c = crc.crc32(data + i, len);
      if (flags & 1U) c ^= 0x5A5A5A5AU;
      wire.push_back((uint8_t)(len >> 8)); wire.push_back((uint8_t)len);
      wire.insert(wire.end(), data + i, data + i + len);
      for (int s = 24; s >= 0; s -= 8) wire.push_back((uint8_t)(c >> s));
      i += len;
    }
  }
  g_stream.reset(wire.data(), wire.size());
  pPrimarySerial = &g_stream;
  // Pump the main-loop comms calls until the input is consumed and no transfer is in progress.
  // (speeduino.ino loop(): transmit if a transfer is in progress, receive if bytes are waiting.)
  // A command that waits for bytes that never come would wait forever on the device; stop once
  // nothing has moved (no byte read or written, same state) for longer than the firmware's 400 ms
  // receive timeout takes to expire on the harness clock.
  unsigned idle = 0;
  for (unsigned iter = 0; iter < 100000U && idle < 1024U; ++iter) {
    const size_t pos = g_stream.pos, wr = g_stream.written;
    const SerialStatus st = serialStatusFlag;
    bool busy = false;
    if (serialTransmitInProgress()) { serialTransmit(); busy = true; }
    if ((g_stream.available() > 0) || serialRecieveInProgress()) { serialReceive(); busy = true; }
    if (!busy) break;
    idle = (pos == g_stream.pos && wr == g_stream.written && st == serialStatusFlag) ? idle + 1 : 0;
  }
  return 0;
}
