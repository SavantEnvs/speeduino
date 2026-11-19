// Upstream's speeduino/comms.cpp, compiled unchanged into the fuzz build in its place, plus a reset
// hook for its file-static transfer state (receive/transmit progress, payload buffer, CRC accumulators,
// EEPROM write deferral). The harnesses call it for every input so each input starts from power-on.
#include "comms.cpp"

void fuzz_reset_comms(void) {
  serialBytesRxTx = 0;
  serialReceiveStartTime = 0;
  CRC32_serial = FastCRC32();
  CRC32_calibration = FastCRC32();
#ifdef COMMS_SD
  SDcurrentDirChunk = 0; SDreadStartSector = 0; SDreadNumSectors = 0; SDreadCompletedSectors = 0;
#endif
  memset(serialPayload, 0, sizeof(serialPayload));
  serialPayloadLength = 0;
  deferEEPROMWritesStart = 0;
  deferEEPROMWritesDelay = 0;
}
