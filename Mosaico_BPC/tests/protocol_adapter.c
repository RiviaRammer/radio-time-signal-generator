#include "../main/bpc_protocol.h"
int encode_for_test(int year, int month, int day, int hour, int minute, int second,
                    int weekday, uint16_t *out)
{
    struct tm t = {0};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = second;
    t.tm_wday = weekday;
    return bpc_encode(&t, out);
}
