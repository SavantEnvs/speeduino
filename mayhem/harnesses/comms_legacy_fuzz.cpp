// Upstream's speeduino/comms_legacy.cpp, compiled unchanged into the fuzz build in its place, plus a
// reset hook for its file-static protocol state (current page/command, the pending legacy 'M' chunk
// write), which upstream gives no way to reach from outside the file. The harnesses call it for every
// input, so an input cut off mid-write cannot steer where the next input's bytes land.
#include "comms_legacy.cpp"

void fuzz_reset_legacy(void) {
  currentPage = 1;
  currentCommand = 0;
  chunkPending = false;
  chunkComplete = 0;
  chunkSize = 0;
  valueOffset = 0;
  firstCommsRequest = true;
  logItemsTransmitted = 0;
  inProgressLength = 0;
}
