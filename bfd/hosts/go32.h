#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <string.h>
#include <sys/file.h>
#ifndef O_ACCMODE
#define O_ACCMODE (O_RDONLY | O_WRONLY | O_RDWR)
#endif
#define SEEK_SET 0
#define SEEK_CUR 1

#define	POSIX_UTIME
#define NO_FCNTL

/* EXACT TYPES */
 typedef char int8e_type;
 typedef unsigned char uint8e_type;
 typedef short int16e_type;
 typedef unsigned short uint16e_type;
 typedef int int32e_type;
 typedef unsigned int uint32e_type;
 
 /* CORRECT SIZE OR GREATER */
 typedef char int8_type;
 typedef unsigned char uint8_type;
 typedef short int16_type;
 typedef unsigned short uint16_type;
 typedef int int32_type;
 typedef unsigned int uint32_type;

/* Macros for the 'type' part of an fopen, freopen or fdopen. 
	<Read|Write>[Update]<Binary file><text file>
 */
#define FOPEN_RB	"rb"
#define FOPEN_WB 	"wb"
#define FOPEN_AB 	"ab"
#define FOPEN_RUB 	"r+b"
#define FOPEN_WUB 	"w+b"
#define FOPEN_AUB 	"a+b"
#define FOPEN_RT	"r"
#define FOPEN_WT 	"w"
#define FOPEN_AT 	"a"
#define FOPEN_RUT 	"r+"
#define FOPEN_WUT 	"w+"
#define FOPEN_AUT 	"a+"
