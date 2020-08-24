#include <stdbool.h>

static volatile bool _pinMarker_active = false;

void  __attribute__ ((noinline)) _magic_pin_start(){_pinMarker_active=true;};
void  __attribute__ ((noinline)) _magic_pin_stop(){_pinMarker_active=false;};