#include "PICA_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

//logging functions are not thread-safe, call them only from the main thread

static int PICA_loglevel = PICA_LOG_INFO;

void PICA_fatal(const char *fmt,...)
{
va_list args;

va_start(args, fmt);
PICA_log_entry(PICA_LOG_FATAL, fmt, args);
va_end(args);
exit(-1);
}

void PICA_error(const char *fmt,...)
{
va_list args;

va_start(args, fmt);
PICA_log_entry(PICA_LOG_ERROR, fmt, args);
va_end(args);
}

void PICA_warn(const char *fmt,...)
{
va_list args;

va_start(args, fmt);
PICA_log_entry(PICA_LOG_WARN, fmt, args);
va_end(args);
}

void PICA_info(const char *fmt,...)
{
va_list args;

va_start(args, fmt);
PICA_log_entry(PICA_LOG_INFO, fmt, args);
va_end(args);
}

void PICA_debug1(const char *fmt,...)
{
if (PICA_loglevel >= PICA_LOG_DBG1) 
	{
	va_list args;

	va_start(args, fmt);
	PICA_log_entry(PICA_LOG_DBG1, fmt, args);
	va_end(args);
	}
}

void PICA_debug2(const char *fmt,...)
{
if (PICA_loglevel >= PICA_LOG_DBG2) 
	{
	va_list args;

	va_start(args, fmt);
	PICA_log_entry(PICA_LOG_DBG2, fmt, args);
	va_end(args);
	}
}

void PICA_debug3(const char *fmt,...)
{
if (PICA_loglevel >= PICA_LOG_DBG3)
	{
	va_list args;

	va_start(args, fmt);
	PICA_log_entry(PICA_LOG_DBG3, fmt, args);
	va_end(args);
	}
}

void PICA_log_entry(int loglevel, const char *fmt, va_list args)
{
static char buf[1024];
static const char *loglevel_txt[] = {"fatal", "error", "warning", "info", "debug1", "debug2", "debug3"};
FILE *log_file = stdout;
time_t log_time;
static char timebuf[32];

if (loglevel <= PICA_LOG_WARN)
	log_file = stderr;

log_time = time(NULL);
strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&log_time));


sprintf(buf, "[%.32s] %s: %.1000s\n", timebuf, loglevel_txt[loglevel - PICA_LOG_FATAL], fmt);

vfprintf(log_file, buf, args);
fflush(log_file);
}

void PICA_set_loglevel(int loglevel)
{
if (loglevel >= PICA_LOG_FATAL && loglevel <= PICA_LOG_DBG3)
	PICA_loglevel = loglevel;
}