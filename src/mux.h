#include <stdint.h>
#include <stddef.h>
int modem_read_mux(int timeout_ms, uint8_t *dlc, uint8_t *typ, char * frame, size_t framemax);
int modem_write_mux(uint8_t dlc, const char *buf, size_t len);
int modem_mux_connect_dlc(uint8_t dlc);
