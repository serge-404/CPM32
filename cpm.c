/***********************************************************************/
/*           CP/M-80 V2.2 emulator for WIN32 v0.4                      */
/*                        cpm.exe                                      */
/*         Copyright (C) 2004-2012 by Keiji Murakami                   */
/***********************************************************************/
/*  2005/09 FCB rc 8bits->7bits (for PL/I-80 Linker)              K.M  */
/*  2005/12 Ctrl-C trap                                           K.M  */
/*  2007/01 delete CR on stdout                                   K.M  */
/*  2008/12 modify FCB reuse method (for TURBO PASCAL)            K.M  */
/*  2012/02 screen clear char change \0 to ' ' (for wineconsole)  K.M  */
/*  2012/02 BDOS 36 (set random record) bug-fix (for UNARC)       K.M  */
/*  2012/03 DEL code, debug message bug-fix                       N.F  */
/*  2012/03 adapt to VC++                                    N.F, K.M  */
/*  2012/03 add option -C (args to uppercase) (for ZLINK)    N.F, K.M  */
/*  2012/03 FCB terminate on [=:;<>]                         N.F, K.M  */
/*  2012/03 Fix screen control function                      N.F, K.M  */
/***********************************************************************/

/* for BORAND C++ */
#ifdef __BORLANDC__
#pragma resource "cpm.res"
#endif

/* for VC++ 2008/2010 */
#ifdef _MSC_VER
#pragma warning(disable:4996)
#endif

#ifdef __linux__
#include <ctype.h>
#include <libgen.h>
#define CH_SLASH '/'
#define _GNU_SOURCE
#define FOREGROUND_INTENSITY 0x0008 // text color is intensified.
#define COMMON_LVB_REVERSE_VIDEO   0x4000 // DBCS: Reverse fore/back ground attribute.
#define COMMON_LVB_UNDERSCORE      0x8000 // DBCS: Underscore.
typedef unsigned int	UINT;
typedef unsigned char	UCHAR;
typedef unsigned char	BYTE;
typedef unsigned short	USHORT;
typedef short			SHORT;
typedef unsigned short	WORD;
typedef unsigned long	ULONG;
typedef unsigned long	DWORD;
typedef struct _COORD {
    SHORT X;
    SHORT Y;
} COORD, *PCOORD;
#define APPEXT ""
#define APPTARGET "LINUX"
#else
#include <windows.h>
#define CH_SLASH '\\'
#define F_OK 0
#define APPEXT ".EXE"
#define APPTARGET "WIN32"
#endif
#define APPVERSION "0.5"
#define CON_BUF_EMPTY -1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#ifdef __linux__
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/statvfs.h>
#include <sys/ioctl.h>
#include <fnmatch.h>
#include "ansiesc.h"
#include "conio.h"
#else
#include <io.h>
#include <conio.h>
#define PATH_MAX MAX_PATH
#endif
#include <fcntl.h>

#ifdef EM180
#include "em180.h"
#else
#define EMST_STOP 0
#define EMST_HALT 1
#define EMST_FUNC 0x100
#define EMST_UNKOWN 0x200
/* from RunCPM/globals.h */
typedef signed char     int8;
typedef signed short    int16;
typedef signed int      int32;
typedef unsigned char   uint8;
typedef unsigned short  uint16;
typedef unsigned int    uint32;
#define byte uint8
#define word uint16
uint8 mem[ 0x10000];
uint8 guard[ 0x10];
#include "ram.h"
#include "cpu.h"
#endif

#define TRUE  1
#define FALSE 0

#define MAXFCB 64

int debug_flag;
#define DEBUGOUT if (debug_flag) fprintf

/* ============ Configuration variable ============ */
byte kbwait;		/* wait on console status check */
int pause_flag;		/* pause before exit */
int retcode_flag;	/* exit code type: 0..none 1:HI-TECH C 2:BDSC */
int no_auto_assign;	/* disable auto drive assign */
int uppercase_flag;	/* force to uppercase */
int NoKOI=FALSE;	/* prevent AltToKIO8 decoding */
int R1715=FALSE;	/* Robotron1715 terminal support */
int adm3a=FALSE;	/* Kaypro termial support */ 
int inversed=FALSE;

enum { RC_HITECH = 1, RC_BDSC};

/* ============ I/O callback ============ */
int io_input( int add) {
    DEBUGOUT( stderr, "ERROR: i/o read port:%04x\n", add);
    exit( -1);
    return 0;
}
void io_output( int add, int data) {
    DEBUGOUT( stderr, "ERROR: i/o write port:%04x <- %02x\n", add, data);
    exit( -1);
}

/* word[8] DPH; */
#define DPH_XLT 0      /* DW	xlt	;Address of sector translation table */
#define DPH_WS1 1      /* DW	0,0,0	;Used as workspace by CP/M */
#define DPH_WS2 2
#define DPH_WS3 3
#define DPH_BUF 4      /* DW	dirbuf	;Address of a 128-byte sector buffer; this is the same for all DPHs in the system. */
#define DPH_DPB 5      /* DW	dpb	;Address of the DPB giving the format of this drive. */
#define DPH_CSV 6      /* DW	csv	;Address of the directory checksum vector for this drive. (set=0) */
#define DPH_ALV 7      /* DW	alv	;Address of the allocation vector for this drive. */

#define EMST_BDOS EMST_FUNC
#define EMST_BIOS_BASE (EMST_FUNC+1)

#define RETEM    0xed,0xfd,
#define CALLN(v) 0xed,0xed,(v),
#define JP(x)    0xc3,(byte)(x),(byte)((x)>>8),
#define RET	 0xc9,
#define HALT	 0x76

#define BDOS_ORG 0xff00                         // 0xfe00
#define BIOS_CNT 17
#define BIOS_ORG 0x10000-(3*BIOS_CNT)-3         // 0xff00
#define BIOS_DPH BIOS_ORG-16                    // 16=sizeof(TDPH)

#define DMY_DPB   (BDOS_ORG + 16)
#define DMY_DPB_SFT (DMY_DPB + 2)
#define DMY_DPB_MSK (DMY_DPB + 3)
#define DMY_DPB_MAX (DMY_DPB + 5)
#define DMY_ALLOC (BDOS_ORG + 32)
#define MAXBLK (128*8)                          // 32 bytes ALLOC

#define setword( p, x) { (p) = (byte)(x); (p) = (byte)((x)>>8); }

/* ================== CP/M BDOS emulation ================== */

#define MAXDRV 16

byte cpm_usr_no = 0;
byte cpm_disk_no = 'B'-'A';
word cpm_dma_addr = 0x80;
word cpm_version = 0x22;
word cpm_disk_vct = 0;
char *cpm_drive[ MAXDRV];

int abort_submit;

FILE *punfp, *rdrfp, *lstfp;

char StartDir[PATH_MAX+1];
char filename[ 1024];
char filename2[ 1024];

#define CPMPATH "CPMPATH"
#ifdef __linux__
static unsigned short pseudograph[]={
  0x9691, 0x9692, 0x9693, 0x9482, 0x94a4, 0x95a1, 0x95a2, 0x9596,
  0x9595, 0x95a3, 0x9591, 0x9597, 0x959d, 0x959c, 0x959b, 0x9490,
  0x9494, 0x94b4, 0x94ac, 0x949c, 0x9480, 0x94bc, 0x959e, 0x959f,
  0x959a, 0x9594, 0x95a9, 0x95a6, 0x95a0, 0x9590, 0x95ac, 0x95a7,
  0x95a8, 0x95a4, 0x95a5, 0x9599, 0x9598, 0x9592, 0x9593, 0x95ab, 
  0x95aa, 0x9498, 0x948c, 0x9688, 0x9684, 0x968b, 0x9690, 0x9680};
#else
enum Colors {
    RESET_COLOR,
    RED_TXT,
    GREEN_TXT,
    YELLOW_TXT,
    BLUE_TXT,
    MAGENTA_TXT,
    CYAN_TXT,
    WHITE_TXT
};

#endif
static unsigned char oriKoi[]={
  0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
  0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
  0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF,
  0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
  0xEE, 0xA0, 0xA1, 0xE6, 0xA4, 0xA5, 0xE4, 0xA3, 0xE5, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE,
  0xAF, 0xEF, 0xE0, 0xE1, 0xE2, 0xE3, 0xA6, 0xA2, 0xEC, 0xEB, 0xA7, 0xE8, 0xED, 0xE9, 0xE7, 0xEA,
  0x9E, 0x80, 0x81, 0x96, 0x84, 0x85, 0x94, 0x83, 0x95, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E,
  0x8F, 0x9F, 0x90, 0x91, 0x92, 0x93, 0x86, 0x82, 0x9C, 0x9B, 0x87, 0x98, 0x9D, 0x99, 0x97, 0x9A};

char *auto_drive_assign( char **arg)
{
#ifdef __linux__
    struct stat statbuf;
#endif
    DWORD att;
    char *p;
    static char argbuf[ 256];
    static char *argp = argbuf;
    static int n = 2;

    if ( !no_auto_assign && ( p = strrchr( *arg, CH_SLASH)) != NULL) {
	*p = '\0';
#ifdef __linux__
	att = stat(	*arg, &statbuf);
#else
	att = GetFileAttributes( *arg);
#endif
	*p = CH_SLASH;
	if ( *arg == p || 
#ifdef __linux__
	    ( !att && ( statbuf.st_mode & S_IFDIR ))) {
#else
	    ( att != (DWORD)-1 && ( att & FILE_ATTRIBUTE_DIRECTORY))) {
#endif
	    cpm_drive[ n] = *arg;
	    *arg = argp;
	    argp += sprintf( argp, "%c:%.124s", n++ + 'A', ++p) + 1;
	    *p = '\0';
	}
    }
    return *arg;
}

void setup_disk_vct( void)
{
    int i;
    char envname[ 8];

    cpm_disk_vct = 3;
    for ( i = 2; i < MAXDRV; i++) {
	if ( cpm_drive[ i]) continue;
	sprintf( envname, "CPM%c", i + 'A');
	if (( cpm_drive[ i] = getenv( envname)) != NULL) {
	    cpm_disk_vct |= (word)( 1 << i);
	}
    }
}

char* CharUpperX(char* st)
{
  char* ss=st;
  if (st==NULL) 
		return st;
  while (*ss) {
    *ss=toupper(*ss);
    ss++;
  }
  return st;
}

#ifdef __linux__
/* Constants for MSC pathname functions */
#define _MAX_PATH       260
#define _MAX_DRIVE      3
#define _MAX_DIR        256
#define _MAX_FNAME      256
#define _MAX_EXT        256
#define stricmp strucmp
void _splitpath(char *path, char *drive, char *dir, char *name, char *ext)
{
	char* cc;
	*dir=0; *name=0; *ext=0;
	strncpy(name, (cc=basename(path)) ? cc: "", _MAX_FNAME);
	strncpy(dir,  (cc=dirname(path)) ? cc: "", _MAX_DIR);
	if ((strcmp(dir,".")==0)&&(*path!='.'))
		*dir=0;								
	if ((cc=strstr(name, "."))!=NULL) {
		strncpy(ext, &cc[1], _MAX_EXT);
		*cc='\0';
	}
}

char *buildname(char* dirnm, char* filenm)
{
	char *cp;
	static char buf[_MAX_DIR];

	if ((dirnm == NULL) || (*dirnm == '\0'))
		return filenm;
	if (filenm == NULL)
		return dirnm;
	if ((cp = strrchr(filenm, '/')) != NULL)
		filenm = cp + 1;
	strcpy(buf, dirnm);
	strcat(buf, "/");
	strcat(buf, filenm);
	return buf;
}

void _makepath(char *path, char *drive, char *dir, char *name, char *ext)
{
  char *cc;
  if (path) {
	*path=0; 
	if ((name==NULL)&&(ext==NULL))
	   strncpy(path, dir, _MAX_DIR);
    else {
		cc=buildname(dir,name);
	    if (cc) strncpy(path, cc, _MAX_DIR);
	}
	if (ext) {
		if (*ext!='.')
			strcat(path, ".");
		strncat(path, ext, 4);		/* CPM have 3-char ext + dot */
	}
  }
}

/* Find file in pathes:
 * 1. /name or ./name or ../name is already qualified names
 * 2. else search in all pathes described in env var CPMPATH
 *    (if this var is not exist, StartDir is used)
 * 3. else search in current directory
 * 4. else return NULL 
 */
char* findPath(char* path, char* env)
{
	char *p, *envp;
	static char name[_MAX_PATH+1];

	if (*path == '/' ||   	/* qualified name */
	    *path == '.')
		return path;
	if ((envp = getenv(env)) != NULL)   /* lookup all pathes */
	  while (*envp) {
		p = name;
		while (*envp && (*p = *envp++) != ':') {
			if ((uint)(p - name) >= sizeof(name))
				break;
			++p;
		}
		if (*--p != '/')
			*++p = '/';
		++p;
		if ((p - name) + strlen(path) >= sizeof(name))
			break;
		strcpy(p, path);
		if (access(name, F_OK) == 0)
			return name;
	  }
    /* search within startdir */
	p=buildname(StartDir, path);
	if (access(p, F_OK) == 0)	/* file exist in current dir */
		return p;
	return NULL;
}

char* _searchenv(char *file, char *varname, char *buf)
{
	char* cc;
    return strncpy(buf, (cc=findPath(file,varname)) ? cc : "", _MAX_DIR);
}

/* CASE insensitive string compare */
int strucmp(char *d, char *s)	
{
	register char c1, *s1 = (char *)d, *s2 = (char *)s, c2;

	while ( ( c1 = ((c1=*s1++) > 0x60 ? c1 & 0x5F : c1 )) == ( c2 = ((c2=*s2++) > 0x60 ? c2 & 0x5F : c2 )) && c1)
		;
	return c1 - c2;
}
#endif

char scpm[5]="cpm";
char scom[5]="com";

int load_program( char *pfile)
{
    FILE *fp;
#ifdef __linux__
    char* drv="";
#else
    char drv[ _MAX_DRIVE];
#endif	
	char dir[ _MAX_DIR], fname[ _MAX_FNAME], ext[ _MAX_EXT];

   _splitpath( pfile, drv, dir, fname, ext);
#ifdef __linux__
	if ((access(pfile, F_OK) != 0) && uppercase_flag) {
		CharUpperX(fname);
		CharUpperX(ext);
		CharUpperX(scpm);
		CharUpperX(scom);
   }
#endif
   if ( drv[ 0] == '\0' && dir[ 0] == '\0') {
	   if ( ext[ 0] == '\0') {
		   _makepath( filename2, drv, dir, fname, scpm);
		   _searchenv( filename2, CPMPATH, filename);
		   if ( filename[ 0] == '\0') {
			   _makepath( filename2, drv, dir, fname, scom);
			   _searchenv( filename2, CPMPATH, filename);
		   }
	   } else {
		   _makepath( filename2, drv, dir, fname, ext);
		   _searchenv( filename2, CPMPATH, filename);
	   }
	   if ( filename[ 0] == '\0') return FALSE;
	   if (( fp = fopen( filename, "rb")) == NULL) return FALSE;
	   _splitpath( filename, drv, dir, fname, ext);
   } else if ( ext[ 0] == '\0'){
	   _makepath( filename, drv, dir, fname, scpm);
	   if (( fp = fopen( filename, "rb")) == NULL) {
		   _makepath( filename, drv, dir, fname, scom);
		   if (( fp = fopen( filename, "rb")) == NULL) return FALSE;
	   }
	   _splitpath( filename, drv, dir, fname, ext);
   } else {
	   if (( fp = fopen( pfile, "rb")) == NULL) return FALSE;
   }
	fread( mem + 0x100, 1, BDOS_ORG-0x100, fp);
	fclose( fp);
    if ( stricmp( ext, "COM") == 0)
		cpm_version = 0x122;
    cpm_drive[ 0] = (char *)malloc( strlen( drv) + strlen( dir) + 1);
#ifdef __linux__
	strcpy( cpm_drive[ 0], dir);
#else
	_makepath( cpm_drive[ 0], drv, dir, NULL, NULL);
#endif
//    cpm_drive[ 1] = "";
    return TRUE;
}

void mkFCB( byte *p, char *s)
{
    int i, c;

    *p = 0;
    if ( s[ 1] == ':') {
        if (( c = *s) >= 'a') c &= 0x5f;
        *p = (byte)( c - 'A' + 1); s += 2;
    }
    p++;
    for ( i = 0; i < 8; i++) {
        c = ( *s && *s != '.') ? *s++ : ' ';
//	if ( c >= 'a' && c <= 'z') c &= 0x5f;
        *p++ = (byte)c;
    }
    while ( *s && *s != '.') s++;
    if ( *s) s++;
    for ( i = 0; i < 3; i++) {
        c = ( *s) ? *s++ : ' ';
//	if ( c >= 'a' && c <= 'z') c &= 0x5f;
        *p++ = (byte)c;
    }
}

char *mk_filename( char *s, byte *fcb)
{
    int i, j;
    char *dir;
#ifdef __linux__
    char *dot=NULL;
#endif
    i = fcb[ 0];
    if ( i == '?' || i == 0) i = cpm_disk_no;
    else i--;
    if ( i >= MAXDRV) return NULL;
    if ( cpm_drive[ i] == NULL) {
	if ( cpm_disk_vct == 0) setup_disk_vct();
	if ( cpm_drive[ i] == NULL) return NULL;
    }
    strcpy( s, cpm_drive[ i]);
    s += i = strlen( s);
    if ( i && s[ -1] != CH_SLASH && s[ -1] != '/' && s[ -1] != ':') *s++ = '/';
    dir = s;

    for ( j = 8; j >= 1 && fcb[ j] == '?'; j--);
    j++;
    for ( i = 1; i < j; i++) {
		if ( fcb[ i] == ' ') break;
		*s++ = fcb[ i];
    }
    if ( i == j && i <= 8) *s++ = '*';

#ifdef __linux__
	dot=s;
#endif
    *s++ = '.';

    for ( j = 11; j >= 9 && fcb[ j] == '?'; j--);
    j++;
    for ( i = 9; i < j; i++) {
	if ( fcb[ i] == ' ') break;
		*s++ = fcb[ i];
#ifdef __linux__
		dot=NULL;
#endif
    }
    if ( i == j && i <= 11) *s++ = '*';
#ifdef __linux__
	if (dot) s=dot;
#endif
	*s = '\0';
    return dir;
}

struct {
    FILE *fp;
    unsigned pos;
    byte *addr;
    byte wr, mod, dmy1, dmy2;
} fcbs[ MAXFCB];

struct FCB {
    byte dr, f[ 8], t[ 3];
    byte ex, s1, s2, rc, d[ 16];
    byte cr, r0, r1, r2;
};

#ifdef __linux__
#define HANDLE void*
#define INVALID_HANDLE_VALUE (void*)0
#define FILE_ATTRIBUTE_DIRECTORY S_IFDIR
#define FILE_ATTRIBUTE_HIDDEN 2
#define chsize ftruncate
#endif

HANDLE hFindFile = INVALID_HANDLE_VALUE;

#ifdef __linux__
typedef struct {
    DWORD dwFileAttributes;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    char  cFileName[ _MAX_PATH ];
    char  cAlternateFileName[ 14 ];
} WIN32_FIND_DATA;

char ffName[_MAX_PATH];
char ffDir[_MAX_PATH];

DIR *FindFirstFile(char* lpFileName,	// pointer to name of file to search for  
    WIN32_FIND_DATA *lpFindFileData) 	// pointer to returned information 
{
    DIR *pDir;
    struct dirent *pDirent;
    struct stat statbuf;
	char *cDir, *cName=lpFileName;
	int szDir;
	cDir=strrchr(lpFileName, '|');
	if (cDir==NULL)
		cDir=strrchr(lpFileName, '/');
	if ((cDir)!=NULL) { 
		cName=cDir+1;
		cDir=lpFileName;
		szDir=cName-cDir-1;
		strncpy(ffDir, cDir, szDir);
		ffDir[szDir]=0;
	}
	else {
		strncpy(ffDir, cpm_drive[ cpm_disk_no], sizeof(ffDir));
	}
	strncpy(ffName, cName, sizeof(ffName)-1);
	if (strncmp(ffName,"*.*",4)==0) {
		*ffName='*';
		ffName[1]=0;
	}
	CharUpperX(ffName);
	pDir = opendir(ffDir);
	if (pDir == NULL) 
        return hFindFile=INVALID_HANDLE_VALUE;
    while ((pDirent = readdir(pDir)) != NULL) {
       	strncpy(lpFindFileData->cFileName, pDirent->d_name, _MAX_PATH-1);
		if (fnmatch(ffName, CharUpperX(pDirent->d_name), FNM_CASEFOLD)==0) {
			if (stat(buildname(ffDir,lpFindFileData->cFileName), &statbuf)!=0) { 
				closedir(pDir);
				DEBUGOUT(stderr, "FindFirst ERROR: failed while stat file `%s`\n",lpFindFileData->cFileName);
	 			return hFindFile=INVALID_HANDLE_VALUE;
			}
			if (!(statbuf.st_mode & S_IFDIR)) {
				lpFindFileData->cAlternateFileName[0]=0;
				lpFindFileData->dwFileAttributes=(*(lpFindFileData->cFileName)=='.' ? FILE_ATTRIBUTE_HIDDEN : 0);	
				lpFindFileData->nFileSizeHigh=0;
				lpFindFileData->nFileSizeLow=(DWORD)statbuf.st_size;
				return pDir;
			}
		}
    }
    closedir(pDir);
    return hFindFile=INVALID_HANDLE_VALUE;
}

int FindNextFile(
    DIR *hFndFile,				    	// handle to search  
    WIN32_FIND_DATA *lpFindFileData) 	// pointer to structure for data on found file  
{
    struct dirent *pDirent;
    struct stat statbuf;
	if (hFndFile==INVALID_HANDLE_VALUE)
		return 0;
    while ((pDirent = readdir(hFndFile)) != NULL) {
       	strncpy(lpFindFileData->cFileName, pDirent->d_name, _MAX_PATH-1);
		if (fnmatch(ffName, CharUpperX(pDirent->d_name), FNM_CASEFOLD)==0) {
			if (stat(buildname(ffDir,lpFindFileData->cFileName), &statbuf)!=0) { 
				DEBUGOUT(stderr, "FindNext ERROR: failed while stat file `%s`\n",lpFindFileData->cFileName);
				break;
			}
			if (!(statbuf.st_mode & S_IFDIR)) {
				lpFindFileData->cAlternateFileName[0]=0;
				lpFindFileData->dwFileAttributes=(*(lpFindFileData->cFileName)=='.' ? FILE_ATTRIBUTE_HIDDEN : 0);	
				lpFindFileData->nFileSizeHigh=0;
				lpFindFileData->nFileSizeLow=(DWORD)statbuf.st_size;
				return 1;
			}
		}
    }
//    if (hFndFile!=INVALID_HANDLE_VALUE) closedir(hFndFile);
//    hFindFile=INVALID_HANDLE_VALUE;
	return 0;
} 

int FindClose(
    DIR *hFndFile 					// file search handle 
   )
{
	int res;
	if (hFndFile && (hFndFile!=INVALID_HANDLE_VALUE)) {
		res=closedir(hFndFile);
		hFindFile=INVALID_HANDLE_VALUE;
		return res==0;
	}
	return TRUE;
}

int GetDiskFreeSpace(
    char* lpRootPathName,			// address of root path 
    DWORD *lpSectorsPerCluster,		// address of sectors per cluster 
    DWORD *lpBytesPerSector,		// address of bytes per sector 
    DWORD *lpNumberOfFreeClusters,	// address of number of free clusters  
    DWORD *lpTotalNumberOfClusters 	// address of total number of clusters  
   )	
{ 
  struct statvfs stat;
  if (statvfs(lpRootPathName, &stat) != 0) {
    // error happens, just quits here
    return 0;
  }
  *lpTotalNumberOfClusters=stat.f_blocks;
  *lpNumberOfFreeClusters=stat.f_bavail;
  *lpBytesPerSector=stat.f_bsize;
  *lpSectorsPerCluster=stat.f_frsize/stat.f_bsize;
  return 1;
}
#endif

byte cpm_findnext( void)
{
    WIN32_FIND_DATA aFindData;
    char *p;
    struct FCB *fcb = (struct FCB *)(mem + cpm_dma_addr);

    if ( hFindFile == INVALID_HANDLE_VALUE) return 0xff;

    while ( FindNextFile( hFindFile, &aFindData)) {
	p = aFindData.cAlternateFileName;
	if ( !*p) p = aFindData.cFileName;
	if (( aFindData.dwFileAttributes & 
		(FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN)) == 0) {
	    DWORD l = aFindData.nFileSizeLow;
	    if ( aFindData.nFileSizeHigh || l > 0x7fff80) l = 0x7fff80;
	    l = (l + 0x7f) >> 7;
	    fcb->ex = (byte)(l >> 7);
	    fcb->rc = (byte)(l & 0x7f);
	    mkFCB( mem + cpm_dma_addr, p);
	    return 0;
	}
    }
    FindClose( hFindFile); 
    hFindFile = INVALID_HANDLE_VALUE;
    return 0xff;
}

byte cpm_findfirst( byte *fcbaddr)
{
    WIN32_FIND_DATA aFindData;
    char *p;
    struct FCB *fcb = (struct FCB *)(mem + cpm_dma_addr);
    DWORD l;

    if ( hFindFile != INVALID_HANDLE_VALUE) FindClose( hFindFile);

    if ( !mk_filename( filename, fcbaddr)) return 0xff;

    hFindFile = FindFirstFile( filename, &aFindData);
    if ( hFindFile == INVALID_HANDLE_VALUE) return 0xff;

    p = aFindData.cAlternateFileName;
    if ( !*p) p = aFindData.cFileName;
    if ( aFindData.dwFileAttributes & 
	 (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN)) {
		return cpm_findnext();
    }

    l = aFindData.nFileSizeLow;
    if ( aFindData.nFileSizeHigh || l > 0x7fff80) l = 0x7fff80;
    l = (l + 0x7f) >> 7;
    fcb->ex = (byte)(l >> 7);
    fcb->rc = (byte)(l & 0x7f);
    mkFCB( mem + cpm_dma_addr, p);
    return 0;
}

#define CFRW_SEQ 0
#define CFRW_RND 1
#define CFRW_RD 0
#define CFRW_WR 1
#define CF_OPEN   0
#define CF_WROPEN 1
#define CF_CREATE 2

byte cpm_file_open( byte *fcbaddr, int cr)
{
    int i;
    int d = 0;
    struct FCB *fcb = (struct FCB *)fcbaddr;
#ifdef __linux__
	DIR *hFndFile;	
	WIN32_FIND_DATA aFindData;
	char dir[ _MAX_DIR], fname[ _MAX_FNAME], ext[ _MAX_EXT];
#endif
    if (( i = fcb->d[ 0]) > 0 && --i < MAXFCB && 
      fcbs[ i].addr == fcbaddr && fcbs[ i].fp) {
DEBUGOUT( stderr, "REOPEN %d - ", i);
      fclose( fcbs[ i].fp); fcbs[ i].fp = NULL;
    } else { // 2008.12 FCB‚ðƒNƒŠƒA‚·‚éƒAƒvƒŠ‚É‘Î‰ž
      for ( i = 0; i < MAXFCB && fcbs[ i].fp; i++) {
        if ( fcbs[ i].addr == fcbaddr) {
           fclose( fcbs[ i].fp); fcbs[ i].fp = NULL; break;
        }
      }
    }
    if ( i >= MAXFCB || !mk_filename( filename, fcbaddr)) return 0xff;

    if ( cr == CF_CREATE) {
		if ( access( filename, F_OK) == 0) return 0xff;
		if (( fcbs[ i].fp = fopen( filename, "w+b")) == NULL) return 0xff;
		fcbs[ i].mod = fcbs[ i].wr = TRUE;
    } else {
	if (( fcbs[ i].fp = fopen( filename, cr ? "r+b" : "rb")) == NULL)
	{
#ifdef __linux__
		if ((hFndFile=FindFirstFile( filename, &aFindData))==INVALID_HANDLE_VALUE) 
			return 0xff;
		_splitpath( filename, NULL, dir, fname, ext);
		fcbs[ i].fp = fopen( buildname(dir, aFindData.cFileName), cr ? "r+b" : "rb");
		FindClose( hFndFile);
		if (fcbs[ i].fp == NULL)
			return 0xff;
#else
		return 0xff;
#endif
	}
	fcbs[ i].mod = fcbs[ i].wr = FALSE;
	fseek( fcbs[ i].fp, 0, SEEK_END);
	d = ftell( fcbs[ i].fp);
	fseek( fcbs[ i].fp, 0, SEEK_SET);
	d = ( d + 0x7f) >> 7;
    }
    fcbs[ i].pos = 0;
    fcbs[ i].addr = fcbaddr;

    memset( fcbaddr + 12, 0, 33-12);
    fcb->d[0] = (byte)(i + 1);
    fcb->d[1] = (byte)d;
    fcb->d[2] = (byte)(d >> 8);
    fcb->d[3] = (byte)(d >> 16);
    fcb->rc = (byte)(( d >= 127) ? 127 : d);
    return 0;
}

byte cpm_file_close( byte *fcbaddr)
{
    int i;
    struct FCB *fcb = (struct FCB *)fcbaddr;

	if (( i = fcb->d[ 0]) > 0 && --i < MAXFCB) {
DEBUGOUT( stderr, "CLOSE %d - ", i);
	    if ( fcbs[ i].fp) fclose( fcbs[ i].fp);
	    fcbs[ i].fp = 0;
	    fcb->d[ 0] = 0;
	    return 0;
	}
	if ( i == 0) return 0;
	return 0xff;
}

void cpm_change_filesize( byte *fcbaddr)
{
    int i, sz;
    struct FCB *fcb = (struct FCB *)fcbaddr;

    sz = (fcb->d[ 3] << 16) + (fcb->d[2] << 8) + fcb->d[1];
    if (( sz >> 7) == fcb->ex && (sz & 0x7f) > fcb->rc &&
	 mk_filename( filename, fcbaddr) &&
	 ( i = open( filename, O_RDWR)) != -1) {
		chsize( i, (( fcb->ex << 7) + fcb->rc) << 7);
		close( i);
DEBUGOUT( stderr, "Change file size : %s\n", filename);
    }
}


byte cpm_file_rw( byte *fcbaddr, byte wr, int rnd)
{
    int i, n, p, max;
    struct FCB *fcb = (struct FCB *)fcbaddr;


    if ( rnd) {
        if ( fcb->r2) return 6;
        p = fcb->r0 + ( fcb->r1 << 8);
        fcb->cr = (byte)(p & 0x7f); fcb->ex = (byte)(p >> 7);
    } else {
        p = (fcb->cr & 0x7f) + (fcb->ex << 7);
    }
    max = fcb->d[1] + (fcb->d[2] << 8) + (fcb->d[3] << 16);

DEBUGOUT( stderr, "BDOS: %s%s file '%11.11s'. @%d", 
      rnd ? "random ": "", wr ? "write" : "read", fcbaddr + 1, p);

    if (( i = fcb->d[ 0]) == 0 || --i >= MAXFCB || fcbs[ i].fp == NULL) {
        if ( !wr && p >= max) return 1;
        if ( cpm_file_open( fcbaddr, wr ? CF_WROPEN : CF_OPEN)) return 0xff;
        fcb->cr = (byte)(p & 0x7f); fcb->ex = (byte)(p >> 7); /* 2012.02 bugfix 8->7 */
        i = fcb->d[ 0] - 1;
    } else if ( !fcbs[ i].mod && wr) {
        fclose( fcbs[ i].fp);
        if ( !mk_filename( filename, fcbaddr) ||
             ( fcbs[ i].fp = fopen( filename, "r+b")) == NULL) {
            cpm_file_close( fcbaddr);
            return 0xff;
        }
        fcbs[ i].pos = 0;
    }

    if ((unsigned)(n = p << 7) != fcbs[ i].pos || fcbs[ i].wr != wr) {
        if (( !wr && p > max) ||
             fseek( fcbs[ i].fp, n, SEEK_SET)) {
            cpm_file_close( fcbaddr);
            return 1;
        }
        fcbs[ i].pos = n;
    }

    if ( wr) {
        fcbs[ i].mod = fcbs[ i].wr = TRUE;
        if ( fwrite( mem + cpm_dma_addr, 128, 1, fcbs[ i].fp) != 1)
             return 1;
        n = 128;
    } else {
        fcbs[ i].wr = FALSE;
        if (( n = fread( mem + cpm_dma_addr, 1, 128, fcbs[ i].fp)) <= 0) {
            if ( !fcbs[ i].mod) cpm_file_close( fcbaddr);
            return 1;
        }
        if ( n < 128) memset( mem + cpm_dma_addr + n, 0x1a, 128 - n);
    }
    fcbs[ i].pos += n;

    ++p;
    if ( !rnd) { fcb->cr = (byte)(p & 0x7f); fcb->ex = (byte)(p >> 7); }
    if ( p >= max && !fcbs[ i].mod) cpm_file_close( fcbaddr);
    if ( p > max) {
        fcb->d[ 1] = (byte)p;
        fcb->d[ 2] = (byte)(p >> 8);
        fcb->d[ 3] = (byte)(p >> 16);
        fcb->rc = (byte)(p & 0x7f);
    }

    return 0;
}

byte cpm_file_size( byte *fcbaddr)
{
    struct FCB *fcb = (struct FCB *)fcbaddr;
    DWORD d;

    if ( cpm_file_open( fcbaddr, CF_OPEN)) return 0xff;
    d = fcb->d[1] + (fcb->d[2] << 8) + (fcb->d[3] << 16);
    fcb->r0 = (byte)d;
    fcb->r1 = (byte)(d >> 8); 
    fcb->r2 = (byte)(d >> 16);
    cpm_file_close( fcbaddr);

    return 0;
}

byte cpm_file_delete( byte *fcbaddr)
{
    struct FCB *fcb = (struct FCB *)fcbaddr;
    int i;
    HANDLE hFndFile;
    WIN32_FIND_DATA aFindData;
    char *fname;
    byte st = 0xff;

    if ( memcmp( fcbaddr, "\1$$$     SUB", 1+8+3) == 0) abort_submit = TRUE;

    if (( i = fcb->d[ 0]) > 0 && --i < MAXFCB && 
		fcbs[ i].addr == fcbaddr && fcbs[ i].fp) {
DEBUGOUT( stderr, "REUSE %d - ", i);
	fclose( fcbs[ i].fp); fcbs[ i].fp = NULL;
    }

    if (( fname = mk_filename( filename, fcbaddr)) == NULL) return 0xff;
    hFndFile = FindFirstFile( filename, &aFindData);
    if ( hFndFile == INVALID_HANDLE_VALUE) return 0xff;
    do {
	char *p = aFindData.cAlternateFileName;
	if ( !*p) p = aFindData.cFileName;
	if ( *p != '.') {
	    strcpy( fname, p);
	    if ( remove( filename) == 0) st = 0;
	}
    } while( FindNextFile( hFndFile, &aFindData));
    FindClose( hFndFile);

    return st;
}


byte cpm_rename( byte *src, byte *dst)
{
    if ( !mk_filename( filename, src)) return 0xff;
    if ( !mk_filename( filename2, dst)) return 0xff;
    return (byte)rename( filename, filename2);
}

byte cpm_set_rndrec( byte *fcbaddr)
{
    struct FCB *fcb = (struct FCB *)fcbaddr;
    unsigned d = (fcb->cr & 0x7f) + (fcb->ex << 7); /* 2012.02 bugfix 8->7 */

    fcb->r0 = (byte)d;
    fcb->r1 = (byte)(d >> 8); 
    fcb->r2 = (byte)(d >> 16);

    return 0;
}

void frameup_dpb_alloc( void)
{
    DWORD spc, bps, free, total;
    DWORD *alloc = (DWORD *)(mem + DMY_ALLOC);
    byte sft;
    int i;

    GetDiskFreeSpace( NULL, &spc, &bps, &free, &total);
    spc *= bps;
    spc >>= 9; sft = 2;
    /* 2008.12 for large disk space (> 2TBytes) */
    while ( spc && (spc & 1) == 0) { spc >>= 1; sft++; }
    free *= spc;
    while ( free > MAXBLK) { free >>= 1; sft++; }
    mem[ DMY_DPB_SFT] = sft;
    *(word *)(mem + DMY_DPB_MSK) = (word)((1 << sft) - 1);

    i = 0;
    while ( free >= 32) {
        free -= 32; alloc[ i++] = 0;
    }
    alloc[ i++] = ~0 << free;
    while ( i < MAXBLK/32) alloc[ i++] = ~0;
}

byte cpm_gettime( byte *buf, time_t t)
{
    int d;
    static time_t time780101;

    if ( time780101 == 0) {
 	struct tm tm;
	memset( &tm, 0, sizeof tm);
	tm.tm_year = 78;
	tm.tm_mon =   0;
	tm.tm_mday =  1;
	time780101 = mktime( &tm);
    }

    t -= time780101;
    d = t % 60; t /= 60;
    buf[ 4] = (byte)((d % 10) + ((d / 10) << 4));
    d = t % 60; t /= 60;
    buf[ 3] = (byte)((d % 10) + ((d / 10) << 4));
    d = t % 24; t /= 24;
    buf[ 2] = (byte)((d % 10) + ((d / 10) << 4));
    d = t + 1;
    buf[ 0] = (byte)d;
    buf[ 1] = (byte)(d >> 8);

    return buf[ 4];
}

byte cpm_disk_reset( void)
{
    HANDLE hFndFile;
    WIN32_FIND_DATA aFindData;

    cpm_disk_no = 0;
    cpm_dma_addr = 0x80;

    sprintf( filename, "%s|*.*", cpm_drive[ cpm_disk_no]);
    hFndFile = FindFirstFile( filename, &aFindData);
    if ( hFndFile == INVALID_HANDLE_VALUE) return 0;
    FindClose( hFndFile);
    return 0xff;
}

/* ================== CP/M LST,RDR,PUN emulation ================== */

void cpm_lst_out( byte c)	/* LST: out */
{
    static int err;

    if ( lstfp == NULL && !err) {
	char *dev = getenv( "CPMLST");
	err = ( lstfp = fopen( dev ? dev : "PRN:", "wb")) == NULL;
    }
    if ( lstfp) putc( c, lstfp);
}

byte cpm_rdr_in( void)		/* RDR: in */
{
    static int err;
    char *dev;
    int c, ct = 16;

    if ( rdrfp == NULL && !err) {
	if (( dev = getenv( "CPMRDR")) != NULL) {
	    err = ( rdrfp = fopen( dev, "r+b")) == NULL;
	} else if ( punfp) {
	    rdrfp = punfp;
	} else {
	    dev = getenv( "CPMAUX");
	    err = ( rdrfp = fopen( dev ? dev : "AUX:", "r+b")) == NULL;
	}
    }
    if ( rdrfp && ( c = getc( rdrfp)) != EOF) return (byte)c;
    if ( --ct == 0) exit( 0);
    return '\x1a';
}

void cpm_pun_out( byte c)	/* PUN: out */
{
    static int err;
    char *dev;

    if ( punfp == NULL && !err) {
	if (( dev = getenv( "CPMPUN")) != NULL) {
	    err = ( punfp = fopen( dev, "r+b")) == NULL;
	} else if ( rdrfp) {
	    punfp = rdrfp;
	} else {
	    dev = getenv( "CPMAUX");
	    err = ( punfp = fopen( dev ? dev : "AUX:", "r+b")) == NULL;
	}
    }
    if ( punfp) putc( c, punfp);
}

/* ================== CP/M consol emulation ================== */
#ifndef __linux__
HANDLE hConOut = INVALID_HANDLE_VALUE;
HANDLE hStdIn = INVALID_HANDLE_VALUE;
#endif
int conout;	/* •W€o—Í‚ÍƒRƒ“ƒ\[ƒ‹H */
int conin;	/* •W€“ü—Í‚ÍƒRƒ“ƒ\[ƒ‹H */
int eofcount = 16;
COORD cpm_cur;

void w32_putch( byte c)
{
#ifdef __linux__
	putchar(c);
	fflush(stdout); 
#else
    DWORD n;
    WriteConsole( hConOut, &c, 1, &n, NULL);
#endif
}

void w32_gotoxy( int x, int y)
{
#ifdef __linux__
	moveTo((short)y,(short)x);
	fflush(stdout);
#else
    COORD cur;
    if ( x > 0) x--;
    if ( y > 0) y--;
    cur.X = (short)x; cur.Y = (short)y;
    SetConsoleCursorPosition( hConOut, cur);
#endif
}

#define cpush(x) {xgch=x;xgch2=CON_BUF_EMPTY;xgch3=CON_BUF_EMPTY;}
#define cpush2(x) {xgch2=x;xgch3=CON_BUF_EMPTY;}
#define cpush3(x) {xgch3=x;}
int xgch=CON_BUF_EMPTY;
int xgch2=CON_BUF_EMPTY;
int xgch3=CON_BUF_EMPTY;

int cpull()					/* FIFO imitation */
{
        int res=xgch;
        xgch=xgch2;
        xgch2=xgch3;
        xgch3=CON_BUF_EMPTY;
        return res;
}

#ifdef __linux__
void getCursorPosition(int *row, int *col) {
	fflush(0);
    printf("\x1b[6n");
    char buff[128];
    int indx = 0;
    for(;;) {
        int cc = getchar();
        buff[indx] = (char)cc;
        indx++;
        if(cc == 'R') {
            buff[indx + 1] = '\0';
            break;
        }
    }    
    sscanf(buff, "\x1b[%d;%dR", row, col);
    fseek(stdin, 0, SEEK_END);
}
#endif

void w32_gotodxy( int dx, int dy)
{
#ifdef __linux__
	int x,y,sx=80,sy=24;
#ifdef TIOCGSIZE
    struct ttysize ts;
    ioctl(STDIN_FILENO, TIOCGSIZE, &ts);
    sx = ts.ts_cols;
    sy = ts.ts_lines;
#elif defined(TIOCGWINSZ)
    struct winsize ts;
    ioctl(STDIN_FILENO, TIOCGWINSZ, &ts);
    sx = ts.ws_col;
    sy = ts.ws_row;
#endif /* TIOCGSIZE */
	getCursorPosition(&y,&x);
	dx+=x;
	dy+=y;
    if ( dx >= sx) dx = sx - 1;
    else if ( dx < 0) dx = 0;
    if ( dy >= sy) dy = sy - 1;
    else if ( dy < 0) dy = 0;
	moveTo((short)dy,(short)dx);
	fflush(stdout);
#else
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo( hConOut, &csbi);
    dx += csbi.dwCursorPosition.X;
    dy += csbi.dwCursorPosition.Y;
    if ( dx >= csbi.dwSize.X) dx = csbi.dwSize.X - 1;
    else if ( dx < 0) dx = 0;
    if ( dy >= csbi.dwSize.Y) dy = csbi.dwSize.Y - 1;
    else if ( dy < 0) dy = 0;
    csbi.dwCursorPosition.X = (short)dx;
    csbi.dwCursorPosition.Y = (short)dy;
    SetConsoleCursorPosition( hConOut, csbi.dwCursorPosition);
#endif	
}

void w32_savexy( void)
{
#ifdef __linux__
	int x,y;
	getCursorPosition(&y,&x);
	cpm_cur.Y=(SHORT)y; cpm_cur.X=(SHORT)x;
#else
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo( hConOut, &csbi);
    cpm_cur = csbi.dwCursorPosition;
#endif
}

void w32_restorexy( void)
{
#ifdef __linux__
	moveTo((short)cpm_cur.Y,(short)cpm_cur.X);
	fflush(stdout);
#else
	SetConsoleCursorPosition( hConOut, cpm_cur);
#endif
}

void w32_cls( int arg)
{
#ifdef __linux__
    if ( arg == 0) {							/* CUR to end of screen */
		clearScreenToBottom();
	} else if ( arg == 1) {					 	/* screen top to CUR */
		clearScreenToTop();
	} else {									/* ALL of SCREEN */
		clearScreen();
    }
	fflush(stdout); 
#else
    DWORD n, size;
    WORD attr;
    COORD cur = {0,0};
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    GetConsoleScreenBufferInfo( hConOut, &csbi);
    attr = csbi.wAttributes;

    // 2012.03 (swap arg=0,1) include CUR by N.Fujita
    if ( arg == 0) {
    	cur = csbi.dwCursorPosition;		/* CUR to end of screen */
	size = csbi.dwSize.X * (csbi.dwSize.Y - csbi.dwCursorPosition.Y)
		- csbi.dwCursorPosition.X;
    } else if ( arg == 1) {
	size = csbi.dwSize.X * csbi.dwCursorPosition.Y 
		+ csbi.dwCursorPosition.X + 1; 	/* screen top to CUR */
    } else {
	size = csbi.dwSize.X * csbi.dwSize.Y;	/* ALL of SCREEN */
    }

    FillConsoleOutputCharacter( hConOut, ' ', size, cur, &n); /* NUL->SP */
    FillConsoleOutputAttribute( hConOut, attr, size, cur, &n);
    if ( arg == 2) SetConsoleCursorPosition( hConOut, cur);
#endif
}

void w32_clrln( int arg)
{
#ifdef __linux__
    if ( arg == 0) {					/* CUR to EOL */
		clearLineToRight();
    } else if ( arg == 1) {				/* TOL to CUR */
		clearLineToLeft();
	} else {							/* ALL LINE */
		clearLine();
	}
	fflush(stdout); 
#else
	DWORD n, size;
    WORD attr;
    COORD cur;
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    GetConsoleScreenBufferInfo( hConOut, &csbi);
    cur = csbi.dwCursorPosition;
    attr = csbi.wAttributes;
    if ( arg == 0) {		/* CUR to EOL */
	size = csbi.dwSize.X - csbi.dwCursorPosition.X;
    } else if ( arg == 1) {	/* TOL to CUR */
	size = csbi.dwCursorPosition.X + 1;  // 2012.03 include CUR
	cur.X = 0;		/* ALL LINE */
    } else {
	size = csbi.dwSize.X;
	cur.X = 0;
    }

    FillConsoleOutputCharacter( hConOut, ' ', size, cur, &n); /* NUL->SP */
    FillConsoleOutputAttribute( hConOut, attr, size, cur, &n);
#endif
}

void w32_scroll( int len)
{
#ifdef __linux__
	if (len<0) {
		while (len++<0)
			insLine();
	} else {
		while (len-->0)
			delLine();
	}								/* TODO */
#else
		COORD dst;
    SMALL_RECT src;
    CHAR_INFO chinfo;
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    GetConsoleScreenBufferInfo( hConOut, &csbi);
    src.Left = 0; 
    src.Top = (short)(csbi.dwCursorPosition.Y + (( len >= 0) ? len : 0));
    src.Right = (short)(csbi.dwSize.X - 1);
    src.Bottom = (short)(csbi.dwSize.Y - 1);
    dst.X = 0;
    dst.Y = (short)(csbi.dwCursorPosition.Y - ((len >= 0) ? 0 : len));
    chinfo.Char.AsciiChar = ' ';
    chinfo.Attributes = csbi.wAttributes;

    ScrollConsoleScreenBuffer( hConOut, &src, NULL, dst, &chinfo);
#endif
}

void w32_up( void)
{
#ifdef __linux__
	moveUp(1); 	fflush(stdout); 
#else
    COORD dst;
    SMALL_RECT src;
    CHAR_INFO chinfo;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo( hConOut, &csbi);

    if ( csbi.dwCursorPosition.Y) {
	csbi.dwCursorPosition.Y--;
	SetConsoleCursorPosition( hConOut, csbi.dwCursorPosition);
    } else {
	src.Left = 0;
	src.Top = 0;
	src.Right = (short)(csbi.dwSize.X - 1);
	src.Bottom = (short)(csbi.dwSize.Y - 2);
	dst.X = 0;
	dst.Y = 1;
	chinfo.Char.AsciiChar = ' ';/* NUL->SP */
	chinfo.Attributes = csbi.wAttributes;
	ScrollConsoleScreenBuffer( hConOut, &src, NULL, dst, &chinfo);
    }
#endif
}

void w32_down( void)
{
#ifdef __linux__
	moveDown(1); 	fflush(stdout); 
#else
    COORD dst;
    SMALL_RECT src;
    CHAR_INFO chinfo;
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    GetConsoleScreenBufferInfo( hConOut, &csbi);
    if ( ++csbi.dwCursorPosition.Y < csbi.dwSize.Y) {
	SetConsoleCursorPosition( hConOut, csbi.dwCursorPosition);
    } else {
	src.Left = 0;
	src.Top = 1;
	src.Right = (short)(csbi.dwSize.X - 1);
	src.Bottom = (short)(csbi.dwSize.Y - 1);
	dst.X = 0;
	dst.Y = 0;
	chinfo.Char.AsciiChar = ' ';/* NUL->SP */
	chinfo.Attributes = csbi.wAttributes;
	ScrollConsoleScreenBuffer( hConOut, &src, NULL, dst, &chinfo);
    }
#endif
}

void w32_left( void)
{
#ifdef __linux__
	moveLeft(1); 	fflush(stdout); 
#else
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo( hConOut, &csbi);
    if ( csbi.dwCursorPosition.X-- > 0) {
	SetConsoleCursorPosition( hConOut, csbi.dwCursorPosition);
    } else {
	csbi.dwCursorPosition.X = csbi.dwSize.X-1;
	SetConsoleCursorPosition( hConOut, csbi.dwCursorPosition);
	w32_up();
    }
#endif
}

void w32_right( void)
{
#ifdef __linux__
	moveRight(1); 	fflush(stdout); 
#else
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo( hConOut, &csbi);
    if ( ++csbi.dwCursorPosition.X < csbi.dwSize.X) {
	SetConsoleCursorPosition( hConOut, csbi.dwCursorPosition);
    } else {
	csbi.dwCursorPosition.X = 0;
	SetConsoleCursorPosition( hConOut, csbi.dwCursorPosition);
	w32_down();
    }
#endif
}

WORD w32_attr( void)
{
#ifdef __linux__
	return 7;		/* text=white, back=black */
#else
	CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo( hConOut, &csbi);
    return csbi.wAttributes;
#endif	
}


enum { ST_START, ST_NOP, ST_CHAR, ST_ESC, ST_EQ, ST_EQ2, ST_ANSI, ST_EQR, ST_1715, ST_BCODE, ST_CCODE};

byte color_table[] = { 0, 4, 2, 6, 1, 5, 3, 7};
byte color_table2[] = { 0, 4, 1, 5, 2, 6, 3, 7};

byte cpm_const( void)
{
#ifdef __linux__
	if (kbhit()) 
		return 0xff;
	else
		return 0;
#else
		DWORD t;
    static DWORD t0, ct;

    if ( conin  ? kbhit() 
		: WaitForSingleObject( hStdIn, 0) != WAIT_TIMEOUT) {
	return 0xff;
    }
    if ( kbwait) {
	t = GetTickCount();
	if ( t != t0) ct = (( t - t0) * 4) >> kbwait;
	if ( ct == 0) Sleep( 1 << (kbwait - 1));
	else --ct;
	t0 = t;
    }
    return 0;
#endif
}

#ifdef __linux__
void my_handler(int s){
//    printf("^C\r\n");
}
#else
BOOL _stdcall console_event_hander( DWORD type)
{
//    DWORD n;
    if ( type != CTRL_C_EVENT) return FALSE;
    /*reg.x.pc*/ PC = 0;
//    WriteConsole( hConOut, "^C\r\n", 4, &n, NULL);
    return TRUE;
}
#define	reset_terminal_mode() ;
#define	set_conio_terminal_mode() ;

#define setTextUnderlined() ;
#define setTextBlinking() ;
#define setTextNoUnderlined() ;
#define setTextNoBlinking() ;
#define setTextNormal() ;
#define saveCursorPosition w32_savexy
#define restoreCursorPosition w32_restorexy

void setCursorOn(void) {
  CONSOLE_CURSOR_INFO curinf;
  if (GetConsoleCursorInfo( hConOut, &curinf)) {
     curinf.bVisible=TRUE;
     SetConsoleCursorInfo( hConOut, &curinf);
  }
}

void setCursorOff(void) {
  CONSOLE_CURSOR_INFO curinf;
  if (GetConsoleCursorInfo( hConOut, &curinf)) {
     curinf.bVisible=FALSE;
     SetConsoleCursorInfo( hConOut, &curinf);
  }
}

void setTextBold(void) {
  SetConsoleTextAttribute( hConOut, w32_attr() | 8);
}

void setTextNoBold(void) {
  SetConsoleTextAttribute( hConOut, w32_attr() & ~8);
}

void w32_inverse(void) {
  WORD cc=w32_attr();
  if (cc>0)
    SetConsoleTextAttribute( hConOut, cc & 0xff00 | ((cc&0xf0)>>4) | ((cc&0x0f)<<4));  /* cc & ~COMMON_LVB_REVERSE_VIDEO */
}

void setTextInverted(void) {
  if (!inversed) {
    inversed=TRUE;
    w32_inverse();
  }
}

void setTextNoInverted(void) {
  if (inversed) {
    inversed=FALSE;
    w32_inverse();
  }
}
#endif

void cpm_conio_setup( void)
{
#ifdef __linux__
	set_conio_terminal_mode();
	conout = TRUE;									/* TODO */
	conin = TRUE;
#else
	DWORD md;
    HANDLE hnd = GetStdHandle( STD_OUTPUT_HANDLE);

    if ( hnd != INVALID_HANDLE_VALUE &&
    	 GetConsoleMode( hnd, &md)) {
	hConOut = hnd;
	conout = TRUE;
    }

    hStdIn = GetStdHandle( STD_INPUT_HANDLE);
    if ( hStdIn != INVALID_HANDLE_VALUE &&
	 GetConsoleMode( hStdIn, &md)) {
	conin = TRUE;
    }
     SetConsoleCtrlHandler( console_event_hander, TRUE);
#endif
}

void cpm_conio_restore( void)
{
#ifdef __linux__
	reset_terminal_mode();
#endif
}

/* 2012.03 replace VC++'s broken getch */
#ifdef _MSC_VER
static int getch( void)
{
    static unsigned keybuf = 0;
    static unsigned char ctltbl[] = { 
      0x84-0x49, 0x76-0x51, 0x75-0x4f, 0x77-0x47, // PgUP, PgDOWN, End, Home,
      0x73-0x4b, 0x8d-0x48, 0x74-0x4d, 0x91-0x50, // LEFT, UP, RIGHT, DOWN,
      0,         0,         0,    0,
      0x92-0x52, 0x93-0x53, 0,    0,     // INS, DEL
    };
    int c = 0x1a;
    DWORD l;
    DWORD mode;
    INPUT_RECORD evt;
//  HANDLE hStdIn = GetStdHandle( STD_INPUT_HANDLE);

    if ( keybuf) { 
        c = (unsigned char)keybuf; 
        keybuf >>= 8; 
        return c;
    }

    GetConsoleMode( hStdIn, &mode);
    SetConsoleMode( hStdIn, 0);
    while ( ReadConsoleInput( hStdIn, &evt, 1, &l)) {
		KEY_EVENT_RECORD *key = &evt.Event.KeyEvent;
		if ( evt.EventType == KEY_EVENT && key->bKeyDown) {
			int k = key->wVirtualKeyCode;
			int s = key->wVirtualScanCode;
			DWORD st = key->dwControlKeyState;
			c = (BYTE)key->uChar.AsciiChar;
			if ( c) break;
			if ( k >= 0x21 && k <= 0x2e) {  // extend keys
				if ( st & (RIGHT_ALT_PRESSED  | LEFT_ALT_PRESSED)) 
				    s += 0x50;
				else if ( st & (RIGHT_CTRL_PRESSED | LEFT_CTRL_PRESSED)) 
				    s += ctltbl[ k - 0x21];
				keybuf = s;
				break;
			}
			if ( k >= 0x70 && k <= 0x7b) {  // F1-F12
				int b = ( k < 0x7a) ? 0x19 : 2;
				int f = ( k < 0x7a) ? 10 : 2;
				if ( k >= 0x7a) s += 0x85-0x57;
				if ( st & (RIGHT_ALT_PRESSED  | LEFT_ALT_PRESSED)) s += b + f + f;
				else if ( st & (RIGHT_CTRL_PRESSED | LEFT_CTRL_PRESSED)) s += b + f;
				else if ( st & SHIFT_PRESSED) s += b;
				keybuf = s;
				break;
			}
		}
	
    }
    SetConsoleMode( hStdIn, mode);

	return c;
}
#endif

/*
^[[5~ PgUp
^[[6~ PgDn
^[[7~ Home
^[[8~ End
^[Od  Ctrl-Ltarw
^[Oc  Ctrl-Rtarw
*/
byte cpm_getch( void)
{
    int c, ch, ch2;
    if ( conin) {
#ifdef __linux__
	  if (( c=cpull()) == CON_BUF_EMPTY)		
	    if (( c = getch()) == 27) {		/* ESC */
		  if (kbhit()) {
			if ((ch=getch()) == '[') {
				    ch2=getch();
					switch ( ch2) {
                		case 'A':  /* Up arrow */
                        	return 'E' - '@';
                		case 'B':  /* Down arrow */
                        	return 'X' - '@';
                		case 'C':  /* Right arrow */
                        	return 'D' - '@';
                		case 'D':  /* Left arrow */
                        	return 8 /*'S' - '@'*/;
                		case '3':  /* Delete key */
                        	getch();
                        	return 'G' - '@';
                		case '2':  /* Insert key */
                        	getch();
                        	return 'V' - '@';
                		case '5':  /* PgUp */
                        	getch();
                        	return 'R' - '@';
                		case '6':  /* PgDn */
                        	getch();
                        	return 'C' - '@';
                		/*case '1':*/			/* CTRL+func.key : `Esc[1;5`+key */
						case '7':   /* Home */
                        	getch();
                		case 'H':   /* Home */
                        	cpush('s');
                       		return 'Q' - '@';	/* WORDSTAR Ctrl+Q+key */
                		case '4':
						case '8':   /* End */
                        	getch();
                		case 'F':   /* End */
                        	cpush('d');
                        	return 'Q' - '@';
                		default:
                        	cpush('[');
                        	cpush2(ch2);
                        	return 27;
                	}
			}
			else if (ch=='O') {
				ch2 = getch();
				switch ( ch2) {
                	case 'A':  /* Up arrow */
                        return 'E' - '@';
                	case 'B':  /* Down arrow */
                        return 'X' - '@';
                	case 'C':  /* Right arrow */
                        return 'D' - '@';
                	case 'D':  /* Left arrow */
                        return 'S' - '@';
                	case 'd':  /* Ctrl left arrow (rxvt) */
                        return 'A' - '@';
                	case 'c':  /* Ctrl right arrow (rxvt) */
                        return 'F' - '@';
                	case 'H':  /* Home */
                        cpush('s');
                        return 'Q' - '@';
                	case 'F':  /* End */
                        cpush('d');
                        return 'Q' - '@';
                	default:
                        cpush('O');
                        cpush2(ch2);
                        return 27;
                }
			}
			else {
				cpush(ch);
				return 27;
			}
		  }
        }
#else
	  if (( c = getch()) == 0) {
	    switch ( getch()) {
	    case 72: c = 'E' - 0x40; break; /* UP */
	    case 80: c = 'X' - 0x40; break; /* DOWN */
	    case 75: c = 8 /*'S' - 0x40*/; break; /* LEFT */            /* 2019 / ORION-128: 8 instead of ^S for LEFT */
	    case 77: c = 'D' - 0x40; break; /* RIGHT */
	    case 82: c = 'V' - 0x40; break; /* INS */
	    case 83: c = '\x7f';     break; /* DEL */ /* 2012.03 bug-fix by N.Fujita */
	    case 73: c = 'R' - 0x40; break; /* PgUP */
	    case 81: c = 'C' - 0x40; break; /* PgDOWN */
	    case 141: c = 'W' - 0x40; break; /* ^UP */
	    case 145: c = 'Z' - 0x40; break; /* ^DOWN */
	    case 115: c = 'A' - 0x40; break; /* ^LEFT */
	    case 116: c = 'F' - 0x40; break; /* ^RIGHT */
	    }
	  }
#endif
    } else if (( c = getchar()) == EOF) {
	  if ( --eofcount == 0) exit( 0);
	  c = '\x1a';
    }
    return (byte)c;
}

byte cpm_getche( void)
{
    int c;

    c = cpm_getch();
    if ( conin) {
#ifdef __linux__
	w32_putch(c);  
#else
    DWORD n;

	if ( c == '\x03') {
		GenerateConsoleCtrlEvent( CTRL_C_EVENT, 0);
	}
	if ( hConOut == INVALID_HANDLE_VALUE) {
	    hConOut = CreateFile( "CONOUT$", GENERIC_READ | GENERIC_WRITE,
	    			  0, NULL, OPEN_EXISTING, 0, NULL);
	}
	WriteConsole( hConOut, &c, 1, &n, NULL);
#endif
    }
	return (byte)c;
}

byte cpm_gets( byte *buf)
{
    int i;
	reset_terminal_mode();
	if ( fgets((char *)buf + 2, buf[ 0], stdin) == NULL) {
		set_conio_terminal_mode();
		if ( --eofcount == 0) {
			cpm_conio_restore();
			exit( 0);
		}
		strcpy((char *)buf + 2, "\x1a\n");
    }
	set_conio_terminal_mode();
    i = strlen((char *)buf + 2);
    if (i > 0) i--;
    buf[ 2 + i] = '\r'; buf[ 1] = (byte)i;
    if ( conout && conin) w32_up();
    return 0;
}

void cpm_putch( int c)
{
    static byte esc_stat, arg_n;
    static byte args[ 8], ct, cb;
#ifndef __linux__
    word cc;
    word t;
#endif
    switch ( esc_stat) {
		case ST_NOP: if ( c != '\r') putchar((char)c);
				return;
		case ST_START:
			if ( !conout) {
				esc_stat = ST_NOP;
				putchar(( byte)c);
				return;
			}
			esc_stat = ST_CHAR;
		case ST_CHAR:
			switch ( c) {
				case '\x0b': w32_up(); 							/* other UP */
						break;
				case 8: w32_left();								/* R1715 left */
						break;
//				case 0x0a: w32_down();							/* R1715 down */
//						break;
//				case 0x0d: w32_gotodxy( -255, 0);				/* Carriage Return */
//						break;
				case 0x14: w32_cls( 0);							/* R1715 CLReos */
						break;
				case 0x15: w32_right();							/* R1715 RIGHT */
						break; 
				case '\x0c': if (R1715) {
									w32_cls( 2);
									w32_gotoxy(1,1);
								}
								else if (adm3a) w32_right(); 
								else w32_left();
							break;
				case '\x1b': esc_stat = ST_ESC; 
							break;
				case '\x1a': if (R1715) 
								w32_up();
							if (! adm3a)						/* else adm3a clear screen + HOME */
								break;
				case '\x1f': w32_cls(2);                        /* 2019 / VT52 of ORION-128: 1F - CLS+HOME  */
				case '\x1e': w32_gotoxy(1,1);					/* HOME */
							break;
				case '\x7f':									/* DEL: echo BS, space, BS */
					printf("\b \b");
					break;
				case 1: w32_gotoxy(1,1);						/* R-1715 HOME */
					break;
#ifdef __linux__
				case 2:	insLine();								/* adm3a insert line */
					break;
				case 3:	delLine();								/* adm3a delete line */
					break;
#else
                                case 2:	w32_scroll( -1);
					break;
				case 3:	w32_scroll( 1);
					break;
#endif
				case 0x17:	if (adm3a)
								w32_cls( 0);					/* R1715 adm3a clreos */
							break;
				case 0x18:	if (R1715)
								w32_gotodxy( -255, 0);    /* or HOME ? */
							if (!adm3a) break;	
				case 5:											/* adm3a clear to eol */
					w32_clrln( 0);
					break;
				case 0x16: if (! R1715)	break;					/* 16 = R1715 CLReol */
				default: if (R1715 && (c==0x82))
							setCursorOff();
						 else if (R1715 && (c==0x83))
							setCursorOn();
						 else if (R1715 && (c==0x84)) {
							setTextNoInverted(); setTextNoBold(); setTextNoUnderlined(); setTextNoBlinking(); setTextNormal();
						 }
						 else {
							 if ((c>128)&&(!NoKOI))
								 c=oriKoi[c & 0x7f];
#ifdef __linux__
							 if ((c>=0xb0)&&(c<0xe0)) {  /* MSDOS pseudograph to ASCII */
								 putchar('\xe2');
								 putchar((char)(pseudograph[c-0xb0]>>8));
								 c=pseudograph[c-0xb0] & 0xff;
							 }
#endif
							 w32_putch((char)c);
						 }
						break;
			}
			return;
		case ST_EQR:											/* ROBOTRON-1715 GotoXY */
			args[ 1] = (byte)(c - 0x80 + 1);
			w32_gotoxy( args[ 1], args[ 0]);
			break;
		case ST_ESC:
			if (c>127) {                                        /* ROBOTRON-1715 GotoXY */
				R1715=TRUE;
				args[ 0] = (byte)(c - 0x80 + 1);
				esc_stat = ST_EQR;
				return;
			}
			else
				switch ( c) {
					case 0x5e:									/* ROBOTRON-1715 escapes */
						esc_stat = ST_1715;
						return;
					case '6': if (inversed) break;              /* 2019/ VT52 of ORION-128: INVERSE ON */
						setTextInverted();
						inversed=TRUE;
						break;
					case '7': if (!inversed) break;                     /* 2019/ VT52 of ORION-128: INVERSE OFF */
						setTextNoInverted();
						inversed=FALSE;
						break;
					case '*': w32_cls( 2); 
							break;
					case 'H': w32_gotoxy(1,1);  					/* 2019/ VT52 of ORION-128:  HOME (áåç î÷èñòêè ýêðàíà) */
							break;
					case ':': setCursorOn();
							break;
					case ';': setCursorOff();
							break;	
					case 'P': 										/* 2019/ VT52 of ORION-128: colors  1=blue, 2=green, 4=red */
						ct=c & GREEN_TXT;							/*TODO								1=red,  2=green, 4=blue */
						if (c & 1) ct+=BLUE_TXT;
						if (c & 4) ct+=RED_TXT;
						if (c & 8)
#ifdef __linux__
                                                        setTextColorBright(ct);			/* ORION-128: 8=intence */
                                                else setTextColor(ct);
#else
                                                        ct &= 8;
#endif
					        c = c>>4;
						cb=c & GREEN_TXT;
						if (c & 1) cb+=BLUE_TXT;
						if (c & 4) cb+=RED_TXT;
						if (c & 8)
#ifdef __linux__
                                                        setBackgroundColorBright(cb);
						else setBackgroundColor(cb);
#else
                                                        cb &= 8;

                				SetConsoleTextAttribute( hConOut, ct | cb << 4 /* | a */);
#endif
						break;
					case 'Y':                                    	/* 2019 - VT52 gotoxy */
					case '=': esc_stat = ST_EQ;
							return;
					case 'B':       /* adm3a enable attribute or Orion GetColor */
						if (adm3a)
							esc_stat = ST_BCODE;
						else {			/* Orion-128 */
							;			/* fill color attributes within current window region */
						}
						break;
					case 'C':       /* adm3a disable attribute or Orion Setcolor */
						esc_stat = ST_CCODE;
						break;
					case '(': 
						setTextBold();
						break;
					case ')':
						setTextNoBold();
						break;
					case 't':
					case 'K':                                        /* 2019/ VT52 of ORION-128: CLREOL - ñòèðàíèå äî êîíöà ñòðîêè (âêë-ÿ ïîçèöèþ êóðñîðà) */
					case 'T': w32_clrln( 0);
						break;
					case 'J':                                        /* 2019/ VT52 of ORION-128: CLREOS - ñòèðàíèå äî êîíöà ýêðàíà (âêëþ÷àÿ ïîçèöèþ êóðñîðà) */
					case 'y': w32_cls( 0);
						break;   // 2012.03 add
					case 'D': w32_down();
						break;
					case 'E': if (! adm3a) {				/* 2019/ VT52 of ORION-128:  CLS */
								w32_cls( 2);
								w32_gotoxy(1,1);
								break;
							  }
					case 'L': w32_scroll( -1);
						break;
					case 'M':								/* was:	w32_up(); break; */
					case 'R': w32_scroll( 1);
						break;
					case '[': arg_n = 0;
							  args[ 0] = args[ 1] = 0;
							  esc_stat = ST_ANSI;
						return;
					default: if (debug_flag) printf( "ESC%c", c);
						break;
				}
			break;
		case ST_1715:			/* ROBOTRON-1715 0x5e escapes */
			switch ( c) {
				case 0x40:
				case 0x44:
					setTextNoInverted(); setTextNoBold(); setTextNoUnderlined(); setTextNoBlinking(); setTextNormal();
					break;
				case 0x41:
				case 0x45:
					setTextBold();
					break;
				case 0x42:
				case 0x46:
					setTextBlinking();
					break;
				case 0x43:
				case 0x47:
					setTextBold();
					setTextBlinking();
					break;
				case 0x50:
				case 0x54:
					setTextNoBold(); setTextNoUnderlined(); setTextNoBlinking(); setTextNormal();
					setTextInverted();
					break;
				case 0x51:
				case 0x55:
					setTextBold();
					setTextInverted();
					break;
				case 0x52:
				case 0x56:
					setTextBlinking();
					setTextInverted();
					break;
				case 0x53:
				case 0x57:
					setTextBold();
					setTextBlinking();
					setTextInverted();
					break;
			}
			return;
		case ST_EQ:
			args[ 0] = (byte)(c - ' ' + 1);
			esc_stat = ST_EQ2;
			return;
		case ST_EQ2:
			args[ 1] = (byte)(c - ' ' + 1);
			w32_gotoxy( args[ 1], args[ 0]);
			break;
		case ST_BCODE:
			if (adm3a) {		/* <ESC>+B prefix */
				switch (c) {
					case '0': /* start reverse video */
						setTextInverted();
						break;
					case '1': /* start half intensity */
						setTextBold();
					break;
					case '2': /* start blinking */
						setTextBlinking();
					break;
					case '3': /* start underlining */
						setTextUnderlined();
					break;
					case '4': /* cursor on */
						setCursorOn();
					break;
					case '6': /* remember cursor position */
						saveCursorPosition();
					break;
					case '5': /* video mode on */
					case '7': /* preserve status line */
						break;
					default:
						cpush(0x1b);
						cpush2('B');
						cpush3(c);
				}
			}
			break;
		case ST_CCODE:
			if (adm3a) {	    /* <ESC>+C prefix */
				switch (c) {
					case '0': /* stop reverse video */
						setTextNoInverted();
						break;
					case '1': /* stop half intensity */
						setTextNoBold();
						break;
					case '2': /* stop blinking */
						setTextNoBlinking();
						break;
					case '3': /* stop underlining */
						setTextNoUnderlined();
						break;
					case '4': /* cursor off */
						setCursorOff();
						break;
					case '6': /* restore cursor position */
						restoreCursorPosition();
						break;
					case '5': /* video mode off */
					case '7': /* don't preserve status line */
						break;
					default:
						cpush(0x1b);
						cpush2('C');
						cpush3(c);
				}
			}
			else {			/* Orion-128 */
				;			/* Select color mode */
			}
			break;
		case ST_ANSI:
			if ( c >= '0' && c <= '9') {
				args[ arg_n] = (byte)(args[ arg_n] * 10 + c - '0');
				return;
			} else if ( c == ';') {
				if ( ++arg_n >= sizeof args) --arg_n;
				args[ arg_n] = 0;
				return;
			} else if ( c == 'H' || c == 'f') {
				w32_gotoxy( args[ 1], args[ 0]);
			} else if ( c == 'A') {
				if ( args[ 0] == 0) args[ 0]++;
				w32_gotodxy( 0, -args[ 0]);
			} else if ( c == 'B') {
				if ( args[ 0] == 0) args[ 0]++;
				w32_gotodxy( 0, args[ 0]);
			} else if ( c == 'C') {
				if ( args[ 0] == 0) args[ 0]++;
				w32_gotodxy( args[ 0], 0);  // 2012.03 ESC[C <=> ESC[D
			} else if ( c == 'D') {
				if ( args[ 0] == 0) args[ 0]++;
				w32_gotodxy( -args[ 0], 0); // 2012.03 ESC[C <=> ESC[D
			} else if ( c == 'J') {
				w32_cls( args[ 0]);
			} else if ( c == 'K') {
				w32_clrln( args[ 0]);
			} else if ( c == 'M') {
				if ( args[ 0] == 0) args[ 0]++; // 2012.03 ESC[M = ESC[1M
				w32_scroll( args[0]);
			} else if ( c == 'L') {
				if ( args[ 0] == 0) args[ 0]++; // 2012.03 ESC[L = ESC[1L
				w32_scroll( -args[0]);
			} else if ( c == 'u') {
				w32_restorexy();
			} else if ( c == 's') {
				w32_savexy();
			} else if ( c == 'm') {
				int i;
				word f = 7, b = 0, a = 0;
				for ( i = 0; i <= arg_n; i++) {
					c = args[ i];
					if ( c >= 30 && c <= 37) {
						f = color_table[ c - 30];
					} else if ( c >= 40 && c <= 47) {
						b = color_table[ c - 40];
					} else if ( c >= 16 && c <= 23) {
						f = color_table2[ c - 16];
					} else if ( c == 1) {
						a |= FOREGROUND_INTENSITY;
					} else if ( c == 7) {
						a |= COMMON_LVB_REVERSE_VIDEO;
					} else if ( c == 4) {
						a |= COMMON_LVB_UNDERSCORE;
					}
				}			
#ifdef __linux__
				if ( a & FOREGROUND_INTENSITY) setTextBold();
					else setTextNoBold();
				if ( a & COMMON_LVB_REVERSE_VIDEO) setTextInverted();
					else setTextNoInverted();
				if ( a & COMMON_LVB_UNDERSCORE) setTextUnderlined();
					else setTextNoUnderlined();
				setTextColor(f);
				setBackgroundColor(b);
#else
				f |= a & FOREGROUND_INTENSITY;
				/* bugbug! REVERSE_VIDEO not work at 2K,XP console */
				if ( a & COMMON_LVB_REVERSE_VIDEO) { t = f; f = b; b = t;}
				a &= COMMON_LVB_UNDERSCORE;									/* WTF? */
				SetConsoleTextAttribute( hConOut, f | (b << 4) | a);
#endif
			} else {
				if (debug_flag) printf( "ESC[n%c", c);
			}
			break;
		default:
			break;
	}
	esc_stat = ST_CHAR;
}

void help( void)
{
    fprintf( stderr, "CPM" APPEXT " -- CP/M-80 program EXEcutor for " APPTARGET " V" APPVERSION "\n"
		"Copyright (C) 2004-2012 by K.Murakami\n"
		"  Usage: CPM [-hxapdCkr][-w[0-9]] command arg1 arg2 ...\n"
		"	-h .. return HI-TECH C exit code\n"
		"	-x .. return ERROR if A:$$$.SUB deleted\n"
		"	-a .. select A: (program directry)\n"
		"	-p .. pause before exit\n"
		"	-d .. disable auto drive assign\n"
		"	-C .. args to uppercase\n"
		"	-8 .. do not KOI8 conversion\n"
		"	-r .. do Robotron-1715 escapes\n"
		"	-k .. do Kaypro(adm3a) escapes\n"
		"	-w[0-9] .. wait on console status check (9:max)\n"
    );
    exit( 1);
}

int main( int argc, char *argv[])
{
    int st, i, p, q;
    char *arg1, *arg2;
    Z80reset();
#ifdef __linux__
    if (getcwd(StartDir, sizeof(StartDir)-1) == NULL) {
       perror("getcwd() error");
       return -1;
   }
	cpm_drive[ cpm_disk_no] = StartDir;
#endif
    for ( i = 1; i < argc; i++) {
	char *s = argv[ i];
	if ( *s != '-') break;
	if ( *++s == '-') { i++; break;}
	while ( *s) {
	    switch ( *s++) {
	    case 'D': debug_flag = TRUE; break;
	    case 'w': kbwait = ( *s >= '0' && *s <= '9') ?
	    				 (*s++ - '0' + 1) : 1;
	    	break;
	    case 'a': cpm_disk_no = 'A' - 'A'; break;
	    case 'p': pause_flag = TRUE; break;
	    case 'h': retcode_flag = RC_HITECH; break;
	    case 'x': retcode_flag = RC_BDSC; break;
	    case 'd': no_auto_assign = TRUE; break;
	    case 'C': uppercase_flag = TRUE; break;
	    case '8': NoKOI=TRUE; break;
		case 'k': adm3a=TRUE; break;
	    case 'r': R1715=TRUE; break;
	    default: help();
	    }
	}
    }
    if ( i >= argc) help();

    if ( !load_program( argv[ i])) {
        fprintf( stderr, "ERROR: program `%s`{.cpm;.com} not found.\n", argv[ i]);
#ifdef __linux__
		fprintf( stderr, "Be careful of case sensitive typeing (or use -C key)!\n");
#endif
        return -1;
    }
	/* setup 0 page */
    p = 0;
    mem[ p++] = 0xC3; setword( mem[ p++], BIOS_ORG + 3);	/* JP WBOOT */
    mem[ p++] = 149;                                            /* IOBYTE: 149 - console CRT(TV); 148 - console TTY(RS232) */
    mem[ p++] = cpm_disk_no;
    mem[ p++] = 0xC3; setword( mem[ p++], BDOS_ORG);		/* JP BDOS */

    /* 2012.03 force to uppercase option */
    if ( uppercase_flag) {
        int j;
        for ( j = i + 1; j < argc; j++) CharUpperX( argv[ j]);
    }

                                   /* setup FCB1,FCB2   */
    arg1 = (i+1 < argc) ? auto_drive_assign( &argv[ i+1]) : "";
    arg2 = (i+2 < argc) ? auto_drive_assign( &argv[ i+2]) : "";

    /* 2012.03 terminate on "=:;<>" */
    p = ( *arg1 && arg1[1] == ':') ? 2 : 0;
    for (; (q = arg1[ p]) != '\0' && !strchr("=:;<>", q); p++);
    if ( q) {
        arg1[ p] = '\0';
        mkFCB( mem + 0x5c, arg1);
        mkFCB( mem + 0x6c, "");
        arg1[ p] = q;
    } else {
        mkFCB( mem + 0x5c, arg1);
        mkFCB( mem + 0x6c, arg2);
    }

    p = 0x81;					   /* setup command buffer */
    for ( i++; i < argc; i++) {
        mem[ p++] = ' ';
        for ( q = 0; p < 0x100-1 && argv[ i][ q]; q++) mem[ p++] = argv[ i][ q];
        if ( p >= 0x100-1) { p = 0x100-1; break;}
    }
    mem[ p] = '\0';
    mem[ 0x80] = p - 0x81;


    /* setup BDOS code */
    p = BDOS_ORG;
    mem[ p++] = 0xED; mem[ p++] = 0xED;                 	/* SPECIAL OPCODE */
    mem[ q=p++] = 0xC9;						/* RET */
    setword( mem[ p++], q);
    setword( mem[ p++], q);
    setword( mem[ p++], q);
    setword( mem[ p++], q);                                     /* BDOS error vectors points to RET */

    mem[ DMY_DPB] = 64;
    mem[ DMY_DPB_SFT] = 4;
    mem[ DMY_DPB_MSK] = (1 << 4) - 1;
    mem[ DMY_DPB_MAX] = (byte) MAXBLK;
    mem[ DMY_DPB_MAX+1] = (byte)(MAXBLK >> 8);

    /* setup BIOS code */
    *((word*)&mem[BIOS_DPH+DPH_WS1])=0;                      /* DW	0,0,0	;Used as workspace by CP/M */
    *((word*)&mem[BIOS_DPH+DPH_WS2])=0;
    *((word*)&mem[BIOS_DPH+DPH_WS3])=0;
    p = BIOS_ORG;
    for ( i = 0; i < BIOS_CNT; i++) {
	mem[ p++] = 0xC3;
        setword( mem[ p++], 3*BIOS_CNT+BIOS_ORG);	     /*   JP l */
    }
    mem[ p++] = 0xED; mem[ p++] = 0xED;                      /* SPECIAL OPCODE */
    mem[ p++] = 0xC9;				             /*   RET */

    /* emulation start */
    cpm_conio_setup();
#ifdef __linux__
   struct sigaction sigIntHandler;
   sigIntHandler.sa_handler = my_handler;
   sigemptyset(&sigIntHandler.sa_mask);
   sigIntHandler.sa_flags = 0;
   sigaction(SIGINT, &sigIntHandler, NULL);
#endif
    memset( guard, HALT, sizeof(guard));
    /* reg.x.r */ *((byte*)&IR) = (byte)time( NULL);
    /* reg.x.sp */ SP = BDOS_ORG - 2;
    /* reg.x.pc */ PC = 0x100;
     st=5;
    while (st != EMST_STOP) {
        Z80run();
        if (StopCode) {
	  if (StopCode==STOP_HALT) {			/* -- HALT -- */
	    fprintf( stderr, "ERROR: Halted at %x.\n", PC-1);
	    return -1;
          }
	  if (StopCode==STOP_OPCODE) {			/* -- OPCODE -- */
	    fprintf( stderr, "ERROR: Wrong opcode at %x.\n", PC-1);
	    return -1;
          }
        }
        else {
	  if ( (word)PC == BDOS_ORG+2) {				/* -- BDOS CALL -- */
	    /* reg.x.hl */ HL = 0;
	    switch ( /* reg.b.c */ (byte)BC ) {
	    case  0: goto cpm_exit; /* return 0; */				/* boot */
	    case  1:					/* con in */
		/* reg.b.l */ *((byte*)&HL) = cpm_getche();
		break;
	    case  2: 		  			/* con out */
		cpm_putch( /* reg.b.e */ (byte)DE );
		break;
	    case  3: 		  			/* RDR: in */
		/* reg.b.l */ *((byte*)&HL) = cpm_rdr_in();
		break;
	    case  4: 		  			/* PUN: out */
		cpm_pun_out( /* reg.b.e */ (byte)DE);
		break;
	    case  5: 		  			/* LST: out */
		cpm_lst_out( /* reg.b.e */ (byte)DE);
		break;
	    case  6:			 		/* direct i/o */
		if ( /* reg.b.e */ (byte)DE == 0xff) {
		    /* reg.b.l */ *((byte*)&HL) = cpm_const() ? cpm_getch() : 0 ;
		} else {
		    cpm_putch( /* reg.b.e */ (byte)DE);
		}
		break;
            case  7:
                *((byte*)&HL) = mem[ 3];
                break;
            case  8:
                mem[ 3] = (byte)DE;
                break;
	    case  9: 					/* string out */
		for ( i = (word)DE; mem[ i] != '$'; i++) cpm_putch( mem[ i]);
		break;
	    case 10: 					/* string in */
		cpm_gets( mem + (word)DE);
                break;
	    case 11: 					/* const */
                /* reg.b.l */ *((byte*)&HL) = cpm_const();
                break;
	    case 12:					/* version */
                HL = cpm_version;
                break;
	    case 13:					/* disk reset */
DEBUGOUT( stderr, "1.");
		/* reg.b.l */ *((byte*)&HL) = cpm_disk_reset();
DEBUGOUT( stderr, "BDOS: disk reset -> %02x\n", /* reg.b.l */ (byte)HL);
                break;
	    case 14:					/* select disk */
DEBUGOUT( stderr, "BDOS: select disk %c:\n", /* reg.b.e */ (byte)DE + 'A');
		if ( cpm_disk_vct == 0) setup_disk_vct();
		if ( /* reg.b.e */ (byte)DE >= MAXDRV ||
			cpm_drive[ /* reg.b.e */ (byte)DE] == NULL) {
		    /* reg.b.l */ *((byte*)&HL) = 0xff;
		    break;
		}
                cpm_disk_no = /* reg.b.e */ (byte)DE;
                break;
	    case 15:					/* file open */
DEBUGOUT( stderr, "2.");
		/* reg.b.l */ *((byte*)&HL) = cpm_file_open( mem + (word)DE, CF_OPEN);
DEBUGOUT( stderr, "BDOS: open file %c:'%11.11s'.%d -> %02x\n",
	 mem[ (word)DE] + '@', mem + (word)DE + 1, mem[ (word)DE + 16] - 1, /* reg.b.l */ (byte)HL);
		break;
	    case 16:					/* file close */
DEBUGOUT( stderr, "3.");
		/* reg.b.l */ *((byte*)&HL) = cpm_file_close( mem + (word)DE);
DEBUGOUT( stderr, "BDOS: close file '%11.11s'. -> %02x\n",
	 mem + (word)DE + 1, /* reg.b.l */ (byte)HL);
		cpm_change_filesize( mem + (word)DE);
		break;
	    case 17:					/* file find first */
		/* reg.b.l */ *((byte*)&HL) = cpm_findfirst( mem + (word)DE);
		break;
	    case 18:					/* file find next */
		/* reg.b.l */ *((byte*)&HL) = cpm_findnext();
		break;
	    case 19:					/* file delete */
DEBUGOUT( stderr, "4.");
		/* reg.b.l */ *((byte*)&HL) = cpm_file_delete( mem + (word)DE);
DEBUGOUT( stderr, "BDOS: delete file %c:'%11.11s' ->%02x.\n",
	 mem[ (word)DE] + '@', mem + (word)DE + 1, /* reg.b.l */ (byte)HL);
		break;
	    case 20:					/* sequential read */
DEBUGOUT( stderr, "5.");
		/* reg.b.l */ *((byte*)&HL) = cpm_file_rw( mem + (word)DE, CFRW_RD, CFRW_SEQ);
DEBUGOUT( stderr, "-> %02x\n", /* reg.b.l */ (byte)HL);
		break;
	    case 21:					/* sequential write */
DEBUGOUT( stderr, "6.");
		/* reg.b.l */ *((byte*)&HL) = cpm_file_rw( mem + (word)DE, CFRW_WR, CFRW_SEQ);
DEBUGOUT( stderr, "-> %02x\n", /* reg.b.l */ (byte)HL);
		break;
	    case 22:					/* file create */
DEBUGOUT( stderr, "7.");
		/* reg.b.l */ *((byte*)&HL) = cpm_file_open( mem + (word)DE, CF_CREATE);
DEBUGOUT( stderr, "BDOS: create file %c:'%11.11s'.-> %02x\n",
	 mem[ (word)DE] + '@', mem + (word)DE + 1, /* reg.b.l */ (byte)HL);
		break;
	    case 23:					/* file rename */
		/* reg.b.l */ *((byte*)&HL) = cpm_rename( mem + (word)DE, mem + (word)DE + 16);
		break;
	    case 24:					  /* get disk vct */
		if ( cpm_disk_vct == 0) setup_disk_vct();
		HL = cpm_disk_vct;
		break;
	    case 25:					  /* get disk no */
		/* reg.b.l */ *((byte*)&HL) = cpm_disk_no;
		break;
	    case 26:					  /* set dma addr */
		cpm_dma_addr = (word)DE;
		break;
	    case 27:					  /* get alloc tbl */
DEBUGOUT( stderr, "BDOS: get alloc tbl\n");
		HL = DMY_ALLOC;
		break;
            case 28:
            case 29:
            case 30:
                HL = 0;
                break;
	    case 31:					  /* get disk prm */
DEBUGOUT( stderr, "BDOS: get disk prm\n");
		frameup_dpb_alloc();
		HL = DMY_DPB;
		break;
	    case 32:                                      /* set/get uid */
                if ( /* reg.b.e */ (byte)DE == 0xff) HL = cpm_usr_no;
                else cpm_usr_no = /* reg.b.e */ (byte)DE;
                break;
	    case 33:					/* random read */
DEBUGOUT( stderr, "8.");
		/* reg.b.l */ *((byte*)&HL) = cpm_file_rw( mem + (word)DE, CFRW_RD, CFRW_RND);
DEBUGOUT( stderr, "-> %02x\n", /* reg.b.l */ (byte)HL);
		break;
	    case 34:					/* random write */
DEBUGOUT( stderr, "9.");
		/* reg.b.l */ *((byte*)&HL) = cpm_file_rw( mem + (word)DE, CFRW_WR, CFRW_RND);
DEBUGOUT( stderr, "-> %02x\n", /* reg.b.l */ (byte)HL);
		break;
	    case 35:					/* file size */
DEBUGOUT( stderr, "10.");
		/* reg.b.l */ *((byte*)&HL) = cpm_file_size( mem + (word)DE);
DEBUGOUT( stderr, "BDOS:get file size '%11.11s' -> %02x.\n",
	 mem + (word)DE + 1, /* reg.b.l */ (byte)HL);
		break;
	    case 36:
DEBUGOUT( stderr, "BDOS: set random record '%11.11s'.\n", mem + (word)DE + 1);
		/* reg.b.l */ *((byte*)&HL) = cpm_set_rndrec( mem + (word)DE);
		break;
	    case 37:					/* drive reset */
DEBUGOUT( stderr, "BDOS: drive reset %04x \n", (word)DE); /* 2012.03 bug-fix by N.Fujita */
		break;
	    case 40:					/* random write w0 */
DEBUGOUT( stderr, "11.");
		/* reg.b.l */ *((byte*)&HL) = cpm_file_rw( mem + (word)DE, CFRW_WR, CFRW_RND);
DEBUGOUT( stderr, " with 0 -> %02x\n", /* reg.b.l */ (byte)HL);
		break;
	    case 102:				/* get file time (CP/M3.0) */
		if ( mk_filename( filename, mem + (word)DE)) {
		    struct stat st;
		    stat( filename, &st);
		    cpm_gettime( mem + (word)DE + 0x18, st.st_atime);
		    cpm_gettime( mem + (word)DE + 0x1c, st.st_mtime);
		} else {
		    /* reg.b.l */ *((byte*)&HL) = 0xff;
		}
		break;
	    case 105:				/* get time (CP/M3.0) */
		/* reg.b.l */ *((byte*)&HL) = cpm_gettime( mem + (word)DE, time( NULL));
		break;
	    default:
		fprintf( stderr, "Unsupported BDOS call: %d\n", /* reg.b.c */ (byte)BC);
                break;
	    }
	    /* reg.b.a */ ((byte*)&AF)[1] = /* reg.b.l */ (byte)HL; /* reg.b.b */ ((byte*)&BC)[1] = /* reg.b.h */ ((byte*)&HL)[1];
	  } else if ( (word)PC >= (p=BIOS_ORG) ) {			/* -- BIOS CALL -- */
	    switch ( q=((word)PrevPC-(word)p) ) {
	    case  0: 					  /* cold boot */       /* 0 */
	    case  3:					  /* warm boot */       /* 1 */
cpm_exit:
		  if ( pause_flag) while ( cpm_getch() >= ' ');
		  cpm_conio_restore();	
		  return  retcode_flag == RC_HITECH ? *(word *)(mem + 0x80) :
			retcode_flag == RC_BDSC && abort_submit ? 2 : 0;
	    case  6:		/* const */                                     /* 2 */
         /* reg.b.a */ ((byte*)&AF)[1] = cpm_const();
                break;
	    case  9:		/* conin */                                     /* 3 */
		/* reg.b.a */ ((byte*)&AF)[1] = cpm_getch();
                break;
	    case  12:		/* conout */                                    /* 4 */
		cpm_putch( /* reg.b.c */ (byte)BC);
		break;
	    case  15: 		/* list */                                      /* 5 */
		cpm_lst_out( /* reg.b.c */ (byte)BC);
		break;
	    case  18: 		/* punch */                                     /* 6 */
		cpm_pun_out( /* reg.b.e */ (byte)DE);
		break;
	    case  21: 		/* reader */                                    /* 7 */
		/* reg.b.a */ ((byte*)&AF)[1] = cpm_rdr_in();
		break;
            case  24:           /* home */                                      /* 8 */
            case  30:           /* settrk */                                    /* 10 */
            case  33:           /* setsec */                                    /* 11 */
                break;
            case  27:           /* seldisk */                                   /* 9 */
                HL=0;
                if ( (byte)BC >= MAXDRV) break;
                if ( cpm_drive[ (byte)BC] == NULL) {
	          if ( cpm_disk_vct == 0) setup_disk_vct();
	          if ( cpm_drive[ (byte)BC] == NULL) break;
                }
                frameup_dpb_alloc();
                *((word*)&mem[BIOS_DPH+DPH_XLT])=0;         /* DW	xlt	;Address of sector translation table */
                *((word*)&mem[BIOS_DPH+DPH_BUF])=128;       /* DW	dirbuf	;Address of a 128-byte sector buffer; this is the same for all DPHs in the system. */
                *((word*)&mem[BIOS_DPH+DPH_DPB])=DMY_DPB;   /* DW	dpb	;Address of the DPB giving the format of this drive. */
                *((word*)&mem[BIOS_DPH+DPH_CSV])=0;         /* DW	csv	;Address of the directory checksum vector for this drive. (set=0) */
                *((word*)&mem[BIOS_DPH+DPH_ALV])=DMY_ALLOC; /* DW	alv	;Address of the allocation vector for this drive. */
                cpm_disk_no = (byte)BC;
                HL=BIOS_DPH;
                break;
            case  36:           /* setDMA */                                    /* 12 */
		cpm_dma_addr = (word)BC;
                HL=0;
		break;
            case  45:           /* lststat */                                   /* 15 */
                ((byte*)&AF)[1] = 0;
		break;
            case  48:           /* sectran */                                   /* 16 */
                HL=(word)BC;
		break;
	    default:
		if (q/3<=BIOS_CNT) fprintf( stderr, "ERROR: Unsupported BIOS call: %x(N%d)\n", (word)PrevPC, q/3);
		return -1;
	    }
          }
	}
    }
    return 0;
}
