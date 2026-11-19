// Stored tune image: what the firmware does with the EEPROM contents at boot (init.cpp
// initialiseAll): loadAllPages() decodes every config page and 3D table from storage, doUpdates()
// migrates an image written by an older firmware version (updates.cpp, keyed on the stored EEPROM
// version byte), loadAllCalibrationTables() loads the sensor calibration tables. Then every page is
// read back through the comms accessors (getPageValue / calculatePageCRC32) as TunerStudio would.
//
// Input: the storage image (first 4096 bytes; the rest of storage reads as erased 0xFF).
#include "fuzz_common.h"
#include "page_crc.h"
#include "updates.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  fuzzenv::resetState(0xFF);
  memcpy(fuzzenv::g_storage, data, size < sizeof(fuzzenv::g_storage) ? size : sizeof(fuzzenv::g_storage));
  loadAllPages();
  doUpdates();
  loadAllCalibrationTables();
  volatile uint32_t sink = 0;
  for (uint8_t page = 0; page < MAX_PAGE_NUM; ++page) {
    uint16_t n = getPageSize(page);
    for (uint16_t off = 0; off < n; ++off) sink += getPageValue(page, off);
    sink += calculatePageCRC32(page);
  }
  (void)sink;
  return 0;
}
