// Secondary serial / serial-CAN bridge protocol: secondserial_Command() (speeduino/comms_secondary.cpp),
// called from the firmware main loop whenever bytes wait on the secondary port. Reaches the 'G' CAN
// reply parser (processSecondaryCanReply), the legacy command handler it shares with the primary port,
// sendValues, and - with the TunerStudio protocol selected - the primary serialReceive() path.
//
// Input: [protocol:u8][caninput_source_start_byte:16][caninput_source_num_bytes:u16 LE][stream...]
// The 19-byte header is the configPage9 state a tuner sets for this port; the rest arrives on the port.
#include "fuzz_common.h"
#include "comms_secondary.h"

static constexpr size_t kHeader = 1 + 16 + 2;
static fuzzenv::BufferPort<SECONDARY_SERIAL_T> g_port;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < kHeader) return -1;
  fuzzenv::resetState(0xFF);
  configPage9.enable_secondarySerial = 1;
  configPage9.secondarySerialProtocol = data[0] & 0x0FU;
  memcpy(configPage9.caninput_source_start_byte, data + 1, 16);
  configPage9.caninput_source_num_bytes = (uint16_t)(data[17] | (data[18] << 8));
  g_port.reset(data + kHeader, size - kHeader);
  pSecondarySerial = &g_port;
  pPrimarySerial = &Serial;
  // speeduino.ino loop(): call secondserial_Command() while bytes wait on the port or a transfer it
  // started is still in progress. A command waiting for bytes that never come would wait forever on
  // the device; stop once nothing has moved for longer than the 400 ms receive timeout takes.
  unsigned idle = 0;
  for (unsigned iter = 0; iter < 100000U && idle < 1024U; ++iter) {
    const bool pending = (serialSecondaryStatusFlag != SERIAL_INACTIVE) || (serialStatusFlag != SERIAL_INACTIVE);
    if (g_port.available() <= 0 && !pending) break;
    const size_t pos = g_port.pos, wr = g_port.written;
    const SerialStatus st = serialStatusFlag, st2 = serialSecondaryStatusFlag;
    if (serialTransmitInProgress()) serialTransmit();   // TunerStudio mode replies via the primary pump
    secondserial_Command();
    idle = (pos == g_port.pos && wr == g_port.written && st == serialStatusFlag && st2 == serialSecondaryStatusFlag) ? idle + 1 : 0;
  }
  return 0;
}
