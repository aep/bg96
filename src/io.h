#include <stddef.h>



int modem_start();
int modem_read(int timeout_ms, char * buf, size_t bufmax);

int  modem_write(char * c, size_t l);
int  modem_write_str(char * c);


#if __linux__
#endif


#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"
