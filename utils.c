#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

void log_error(char *message)
{
    fprintf(stderr, "%s:%s:(%s).\n", "Error", message, strerror(errno));
    fflush(stderr);
}
