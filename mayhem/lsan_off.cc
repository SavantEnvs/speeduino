// Leak detection is off for every target (SPEC §6.2 item 15); ASan + UBSan stay fully active.
extern "C" int __lsan_is_turned_off() { return 1; }
