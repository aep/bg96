#include <stdio.h>
#include "io.h"
#include "mux.h"
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

static int modem_read_line(int timeout_ms, char * line, size_t linemax)
{
    int lineat = 0;
    for (;;) {
        char rbuf[2];
        int rdlen = modem_read(timeout_ms, rbuf, 1);
        if (rdlen  != 1) {
            return rdlen;
        }

        line[lineat++] = rbuf[0];

        if (lineat + 1 >= linemax  || rbuf[0] == '\n') {
            line[lineat] = 0;
            if (lineat > 1) {
                printf(KCYN "<< %s" KNRM , line);
                return lineat;
            }
            lineat = 0;
        }
    }
}



static int modem_wait_ok() {
    char line[1000];
    for (;;) {
        int len = modem_read_line(1000, line, sizeof(line));
        if (len == 0) {
            printf("timeout waiting for ATOK\n");
            return 1;
        } else if (len < 0) {
            printf("Error : %d: %s\n", len, strerror(errno));
            return len;
        } else if (strstr(line, "OK") != 0 ){
            return 0;
        }
    }
}




static int extract_response_number_field(char *msg, int fieldnum) {

    size_t max = strlen(msg);
    char delim = ':';

    for (size_t i = 0;i < max; i++) {
        if (msg[i] == delim) {
            if (fieldnum == 0) {
                return atoi(msg + 1 + i);
            }
            fieldnum--;
            delim = ',';
        }
    }
    return 0;
}






static int stat_current_signal=99;




#define MACH_EV_ENTER 1
#define MACH_EV_MSG   2
#define MACH_EV_IDLE  3

typedef int(*statemachine_fn)(int ev, char *msg);

/// state engine for main connection

static statemachine_fn machine_mux1_fn = 0;
static int machine_mux1_move(statemachine_fn next);
static int machine_mux1_msg(char *msg);
static int machine_mux1_idle();
static int machine_mux1_abort(char * reason);

static int machine_mux1_start(int ev, char *msg);
static int machine_mux1_QICSGP(int ev, char *msg);
static int machine_mux1_QIACT(int ev, char *msg);
static int machine_mux1_QIOPEN(int ev, char *msg);
static int machine_mux1_getip(int ev, char *msg);
static int machine_mux1_active(int ev, char *msg);


static int machine_mux1_start(int ev, char *msg) {
    const char *cmd = "AT+CREG?\r\n";

    switch (ev) {
        case MACH_EV_ENTER:
            return modem_write_mux(1, cmd, strlen(cmd));
        case MACH_EV_IDLE:
            return modem_write_mux(1, cmd, strlen(cmd));
        case MACH_EV_MSG: {
            if (strstr(msg, "ERROR") != 0) {
                return machine_mux1_abort("AT+CREG error");
            }
            if (strstr(msg, "+CREG:") != 0) {
                int stat = extract_response_number_field(msg, 1);
                printf("CREG STAT: %d\n", stat);
                if (stat == 1 || stat == 5) {
                    printf("registered!\n");
                    return machine_mux1_move(machine_mux1_QICSGP);
                }
            }
            return 0;
        }
        default:
            return 0;
    }
}

static int machine_mux1_QICSGP(int ev, char *msg) {
    const char *cmd = "AT+QICSGP=1,1,\"iot.1nce.net\"\r\n";
    switch (ev) {
        case MACH_EV_ENTER:
            return modem_write_mux(1, cmd, strlen(cmd));
        case MACH_EV_IDLE:
            return machine_mux1_abort("AT+QICSGP timeout");
        case MACH_EV_MSG: {
            if (strstr(msg, "ERROR") != 0) {
                return machine_mux1_abort("AT+QICSGP error");
            }
            if (strstr(msg, "OK") != 0) {
                return machine_mux1_move(machine_mux1_QIACT);
            }
            return 0;
        }
        default:
            return 0;
    }
}

static int machine_mux1_QIACT(int ev, char *msg) {
    const char *cmd = "AT+QIACT=1\r\n";
    switch (ev) {
        case MACH_EV_ENTER:
            return modem_write_mux(1, cmd, strlen(cmd));
        case MACH_EV_MSG: {
            if (strstr(msg, "ERROR") != 0) {
                return machine_mux1_abort("AT+QIACT error");
            }
            if (strstr(msg, "OK") != 0) {
                return machine_mux1_move(machine_mux1_getip);
            }
            return 0;
        }
        default:
            return 0;
    }
}

static int machine_mux1_getip(int ev, char *msg) {
    const char *cmd = "AT+QIACT?\r\n";
    switch (ev) {
        case MACH_EV_ENTER:
            return modem_write_mux(1, cmd, strlen(cmd));
        case MACH_EV_MSG: {
            if (strstr(msg, "ERROR") != 0) {
                return machine_mux1_abort("AT+QIACT? error");
            }
            if (strstr(msg, "OK") != 0) {
                return machine_mux1_move(machine_mux1_QIOPEN);
            }
            return 0;
        }
        default:
            return 0;
    }
}

static int machine_mux1_QIOPEN(int ev, char *msg) {
    const char *cmd = "AT+QIOPEN=1,0,\"TCP\",\"home.0x.pt\",1883,0,2\r\n";
    switch (ev) {
        case MACH_EV_ENTER:
            return modem_write_mux(1, cmd, strlen(cmd));
        case MACH_EV_MSG: {
            if (strstr(msg, "ERROR") != 0) {
                return machine_mux1_abort("AT+QIOPEN error");
            }
            if (strstr(msg, "CONNECT") != 0) {
                return machine_mux1_move(machine_mux1_active);
            }
            return 0;
        }
        default:
            return 0;
    }
}

static int machine_mux1_active(int ev, char *msg) {
    char buf[1000];

    snprintf(buf, sizeof(buf)-1, "current signal: %d\n", stat_current_signal);

    switch (ev) {
        case MACH_EV_ENTER:
            return 0;
        case MACH_EV_IDLE:
            return modem_write_mux(1, buf, strlen(buf));
        case MACH_EV_MSG: {
            if (strcmp(msg, "\r\nNO CARRIER\r\n") == 0) {
                return machine_mux1_abort("disconnected!\n");
            }
            return 0;
        }
        default:
            return 0;
    }
}


static int machine_mux1_dispatch(int ev, char *msg) {
    if (machine_mux1_fn != 0) {
        return machine_mux1_fn(ev, msg);
    }
    return 0;
}

static int machine_mux1_move(statemachine_fn next) {
    machine_mux1_fn = next;
    return machine_mux1_dispatch(MACH_EV_ENTER, 0);
}

static int machine_mux1_abort(char *reason) {
    printf("%s\n", reason);
    machine_mux1_fn = 0;
    return -1;
}



/// state engine for auxiliary at commands




static statemachine_fn machine_mux2_fn = 0;
static int machine_mux2_move(statemachine_fn next);
static int machine_mux2_msg(char *msg);
static int machine_mux2_idle();
static int machine_mux2_abort(char * reason);

static int machine_mux2_start(int ev, char *msg);
static int machine_mux2_QGPSSUPLURL(int ev, char *msg);
static int machine_mux2_QGPSCFG(int ev, char *msg);
static int machine_mux2_QGPS(int ev, char *msg);
static int machine_mux2_nmeasrc(int ev, char *msg);
static int machine_mux2_QGPSLOC(int ev, char *msg);
static int machine_mux2_QGPSGNMEA_GSA(int ev, char *msg);
static int machine_mux2_QGPSGNMEA_GNS(int ev, char *msg);
static int machine_mux2_csq(int ev, char *msg);
static int machine_mux2_querypause(int ev, char *msg);

static int machine_mux2_start(int ev, char *msg) {
    const char *cmd = "AT+QGPSCFG=\"plane\",0\r\n";

    switch (ev) {
        case MACH_EV_ENTER:
            return modem_write_mux(2, cmd, strlen(cmd));
        case MACH_EV_IDLE:
            return machine_mux2_abort("AT+QGPSCFG timeout");
        case MACH_EV_MSG: {
            if (strstr(msg, "ERROR") != 0) {
                return machine_mux2_abort("AT+QGPSCFG error");
            }
            if (strstr(msg, "OK") != 0) {
                return machine_mux2_move(machine_mux2_QGPSSUPLURL);
            }
            return 0;
        }
        default:
            return 0;
    }
}

static int machine_mux2_QGPSSUPLURL(int ev, char *msg) {
    const char *cmd = "AT+QGPSSUPLURL=\"supl.google.com:7276\"\r\n";

    switch (ev) {
        case MACH_EV_ENTER:
            return modem_write_mux(2, cmd, strlen(cmd));
        case MACH_EV_IDLE:
            return machine_mux2_abort("AT+QGPSSUPLURL timeout");
        case MACH_EV_MSG: {
            if (strstr(msg, "ERROR") != 0) {
                return machine_mux2_abort("AT+QGPSSUPLURL error");
            }
            if (strstr(msg, "OK") != 0) {
                return machine_mux2_move(machine_mux2_QGPSCFG);
            }
            return 0;
        }
        default:
            return 0;
    }
}

static int machine_mux2_QGPSCFG(int ev, char *msg) {
    const char *cmd = "AT+QGPSCFG=\"lbsapn\",16,1, \"iot.1nce.net\"\r\n";

    switch (ev) {
        case MACH_EV_ENTER:
            return modem_write_mux(2, cmd, strlen(cmd));
        case MACH_EV_IDLE:
            return machine_mux2_abort("AT+QGPSCFG timeout");
        case MACH_EV_MSG: {
            if (strstr(msg, "ERROR") != 0) {
                return machine_mux2_abort("AT+QGPSCFG error");
            }
            if (strstr(msg, "OK") != 0) {
                return machine_mux2_move(machine_mux2_nmeasrc);
            }
            return 0;
        }
        default:
            return 0;
    }
}

static int machine_mux2_nmeasrc(int ev, char *msg) {
    const char *cmd = "AT+QGPSCFG=\"nmeasrc\",1\r\n";
    switch (ev) {
        case MACH_EV_ENTER:
            return modem_write_mux(2, cmd, strlen(cmd));
        case MACH_EV_IDLE:
            return machine_mux2_abort("AT+QGPSCFG timeout");
        case MACH_EV_MSG: {
            if (strstr(msg, "ERROR") != 0) {
                return machine_mux2_abort("AT+QGPSCFG error");
            }
            if (strstr(msg, "OK") != 0) {
                return machine_mux2_move(machine_mux2_QGPS);
            }
            return 0;
        }
        default:
            return 0;
    }
}

static int machine_mux2_QGPS(int ev, char *msg) {
    const char *cmd = "AT+QGPS=1\r\n";

    switch (ev) {
        case MACH_EV_ENTER:
            return modem_write_mux(2, cmd, strlen(cmd));
        case MACH_EV_IDLE:
            return machine_mux2_abort("AT+QGPS timeout");
        case MACH_EV_MSG: {
            if (strstr(msg, "ERROR") != 0) {
                return machine_mux2_abort("AT+QGPS error");
            }
            if (strstr(msg, "OK") != 0) {
                return machine_mux2_move(machine_mux2_QGPSGNMEA_GSA);
            }
            return 0;
        }
        default:
            return 0;
    }
}

static int machine_mux2_QGPSGNMEA_GSA(int ev, char *msg) {
    const char *cmd = "AT+QGPSGNMEA=\"GSA\"\r\n";

    switch (ev) {
        case MACH_EV_ENTER:
            return modem_write_mux(2, cmd, strlen(cmd));
        case MACH_EV_IDLE:
            return machine_mux2_abort("AT+QGPSGNMEA timeout");
        case MACH_EV_MSG: {
            if (strstr(msg, "ERROR") != 0) {
                return machine_mux2_move(machine_mux2_QGPSLOC);
            }
            if (strstr(msg, "OK") != 0) {
                return machine_mux2_move(machine_mux2_QGPSLOC);
            }
            return 0;
        }
        default:
            return 0;
    }
}

static int machine_mux2_QGPSLOC(int ev, char *msg) {
    const char *cmd = "AT+QGPSLOC=2\r\n";

    switch (ev) {
        case MACH_EV_ENTER:
            return modem_write_mux(2, cmd, strlen(cmd));
        case MACH_EV_IDLE:
            return machine_mux2_abort("AT+QGPSLOC timeout");
        case MACH_EV_MSG: {
            if (strstr(msg, "ERROR") != 0) {
                return machine_mux2_move(machine_mux2_querypause);
            }
            if (strstr(msg, "OK") != 0) {
                return machine_mux2_move(machine_mux2_querypause);
            }
            return 0;
        }
        default:
            return 0;
    }
}

static int machine_mux2_csq(int ev, char *msg) {
    const char *cmd = "AT+CSQ\r\n";

    switch (ev) {
        case MACH_EV_ENTER:
            return modem_write_mux(2, cmd, strlen(cmd));
        case MACH_EV_IDLE:
            return modem_write_mux(2, cmd, strlen(cmd));
        case MACH_EV_MSG: {
            if (strstr(msg, "ERROR") != 0) {
                return machine_mux2_abort("AT+CSQ error");
            }
            if (strstr(msg, "+CSQ:") != 0) {
                stat_current_signal = extract_response_number_field(msg, 0);
                return machine_mux2_move(machine_mux2_QGPSGNMEA_GSA);
            }
            return 0;
        }
        default:
            return 0;
    }
}

static int machine_mux2_querypause(int ev, char *msg) {
    if (ev == MACH_EV_IDLE) {
        return machine_mux2_move(machine_mux2_csq);
    }
    return 0;
}




static int machine_mux2_dispatch(int ev, char *msg) {
    if (machine_mux2_fn != 0) {
        return machine_mux2_fn(ev, msg);
    }
    return 0;
}

static int machine_mux2_move(statemachine_fn next) {
    machine_mux2_fn = next;
    return machine_mux2_dispatch(MACH_EV_ENTER, 0);
}

static int machine_mux2_abort(char *reason) {
    printf("%s\n", reason);
    machine_mux2_fn = 0;
    return -1;
}


//
//
//
//


int main() {

    modem_start();


    if (1) {

        char line[1000];

        // wait for RDY
        for (;;) {
            int len = modem_read_line(20000, line, sizeof(line));
            if (len == 0) {
                printf("timeout\n");
                return 1;
            } else if (len < 0) {
                printf("Error : %d: %s\n", len, strerror(errno));
                return len;
            } else if (strstr(line, "RDY") != 0 ){
                break;
            }
        }

        modem_write_str("AT\r\n");
        if (modem_wait_ok() != 0) {
            return 1;
        }

        modem_write_str("AT+CMUX=0\r\n");
        if (modem_wait_ok() != 0) {
            return 1;
        }

        sleep(1);
    }

    if (modem_mux_connect_dlc(0) !=0) {
        return 1;
    }
    if (modem_mux_connect_dlc(1) !=0) {
        return 1;
    }
    if (modem_mux_connect_dlc(2) !=0) {
        return 1;
    }


    if (machine_mux1_move(machine_mux1_start) != 0) {
        return 3;
    }
    if (machine_mux2_move(machine_mux2_start) != 0) {
        return 3;
    }


    char line[2000];
    for (;;) {
        uint8_t typ  = 0;
        uint8_t dlc  = 0;
        int len = modem_read_mux(1000, &dlc, &typ, line, sizeof(line) - 1);
        if (len < 0) {
            if (len == -EAGAIN) {
                if (machine_mux1_dispatch(MACH_EV_IDLE, 0) != 0) {
                    return 2;
                }
                if (machine_mux2_dispatch(MACH_EV_IDLE, 0) != 0) {
                    return 2;
                }
                continue;
            }
            printf("Error : %d: %s\n", len, strerror(-len));
            return len;
        }
        line[len] = 0;

        if (typ != 0xEF) {
            printf("Error : expected UIH: 0x%02x\n", typ);
            return 2;
        }

        if (dlc == 1) {
            if (machine_mux1_dispatch(MACH_EV_MSG, line) != 0) {
                return 1;
            }
        } else if (dlc == 2) {
            if (machine_mux2_dispatch(MACH_EV_MSG, line) != 0) {
                return 1;
            }
        }
    }


}

