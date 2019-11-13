/*
	(c) Copyright  2012 - 2018 Anton Sviridenko
	https://picapica.im

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, version 3.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#ifndef PICA_LOG_H
#define PICA_LOG_H

#include <stdarg.h>

#define PICA_LOG_FATAL  31337
#define PICA_LOG_ERROR 31338
#define PICA_LOG_WARN 31339
#define PICA_LOG_INFO 31340
#define PICA_LOG_DBG1 31341
#define PICA_LOG_DBG2 31342
#define PICA_LOG_DBG3 31343

void PICA_fatal(const char *fmt, ...);
void PICA_error(const char *fmt, ...);
void PICA_warn(const char *fmt, ...);
void PICA_info(const char *fmt, ...);
void PICA_debug1(const char *fmt, ...);
void PICA_debug2(const char *fmt, ...);
void PICA_debug3(const char *fmt, ...);

void PICA_log_entry(int loglevel, const char *fmt, va_list args);

void PICA_set_loglevel(int loglevel);
#endif
