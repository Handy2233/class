#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>

#include "lcd.h"
#include "touch.h"
#include "log.h"
