#ifndef FTP_SERVER_H_INCLUDED
#define FTP_SERVER_H_INCLUDED

#include "configs.h"

#if defined(FTP_SERVER_ENABLED) && defined(ESP32)
void ftp_server_begin();
void ftp_server_loop();
#else
static inline void ftp_server_begin() {}
static inline void ftp_server_loop() {}
#endif

#endif // FTP_SERVER_H_INCLUDED
