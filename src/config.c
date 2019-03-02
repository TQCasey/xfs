#include "smfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "private.h"

/* flash offset */
#define OFFSET          (2 << 20) 
#define OFFSET_PAGE     (OFFSET >> 12)
#define ENDSIZE         (16 << 20)

static  int FLASHSIZE   = 0 ;

#ifdef	_WIN32

/* in windows platform 
 */
#define	VDISKNAME		("disk.img")
static	FILE			*fp = NULL;
#else
/* in checkme platform 
 */
 #include <n25q032a.h>
#include "freertos.h"
 
#define VDISKNAME		

#endif

extern struct _fsinfo			*fsinfo;

#if !_WIN32

#define FLASH_SECTOR_SIZE       0           /* FLASH_SECTORS information used */
#define FLASH_PROGRAM_UNIT      2           /* Smallest programmable unit in bytes */
#define FLASH_ERASED_VALUE      0xFF        /* Contents of erased memory */
#define LANG_SIZE               (0x200000)  /* language packet size : 2M */

#define LOGIC_SECTOR_SIZE       (32 * 1024) /* logical 32K sector size */
#define HARD_SECTOR_SIZE        (64 * 1024) /* hardware : 64K sector */
#define ONE_PAGE_SIZE           (0x1000)    /* 4k/page */

extern int start_erase (int max) ;
extern void erasing (int id,int pos,int max) ;
extern int end_erase (int id) ;

int32_t NOR_EraseChip (void) {
	//printf ( "EraseChip\r\n");
    
	unsigned addr = LANG_SIZE,end = FLASHSIZE,start = addr;
    int id = start_erase ((end - start) / HARD_SECTOR_SIZE);  /* 64 numbers */
	
    printf ( "flash size = %d \n",FLASHSIZE);
    
	for (addr = addr ; addr < end ; addr += HARD_SECTOR_SIZE) {     
        /* this place uses the hardware sector size 
         * to ensure the speed of erasing 
         */
        erasing (id,(addr - start) / HARD_SECTOR_SIZE,(end - start) / HARD_SECTOR_SIZE) ;
        erase_sector (addr);
	}
    
    end_erase (id);
  return 0;
}

void erase_lang_area (void) {
	unsigned addr = 0,end = LANG_SIZE,start = addr;
    int id = start_erase ((end - start) / HARD_SECTOR_SIZE);  /* 64 numbers */
	
	for (addr = addr ; addr < end ; addr += HARD_SECTOR_SIZE) {
        erasing (id,(addr - start) / HARD_SECTOR_SIZE,(end - start) / HARD_SECTOR_SIZE) ;
        erase_sector (addr);
	}
    
    end_erase (id);
}

#endif

/* to get the hardware flash memory layout */
int hwl_init (struct _sblk *psblk) 
{
#ifdef	_WIN32
	int size = 0;

	if (NULL == (fp = fopen ("disk.img","rb+"))) {
		return (-SMERR_IO);
	}
	
	size = ENDSIZE;

#else
    u16  id [3];
    unsigned size = 0;
    
    SPIx_IO_Init ();
	SPIx_Init ();
    read_id (id);
    
    switch (id [1] & 0xff) {
        case 0x16:      size = 4 << 20;     break;
        case 0x17:      size = 8 << 20;     break;
        case 0x18:      size = 16 << 20 ;   break;
        default:        size = 4 << 20;     break;
    }
#endif

    if (size > ENDSIZE)
        size = ENDSIZE;
    
    FLASHSIZE = size;
    
    /* sub the offset */
    size -= OFFSET;
    
    XDEBUG (0,"Flash Size = %d",size);
    
    //size -= 160 * 1024;
    
    psblk->page_size		= 4096;							
	psblk->page_nr			= size / 4096 ;						

	return (0);
}

void * MALLOC (int size)
{
#ifdef _WIN32
	return (malloc (size));
#else
	return (pvPortMalloc (size));
#endif
	
}

void *FREE (void *mem)
{
#ifdef _WIN32
	free (mem) ;
#else
	vPortFree  (mem);
#endif
	return (NULL);
}

int hwl_uninit (struct _fsinfo *pinfo) 
{
#ifdef	_WIN32
	if (fp)
		fclose (fp);
	fp = NULL;
#else
	;
#endif
	return (0);
}

int ll_read_bytes (int offset,unsigned char * buffer,int size)
{
	int readn = 0;

#ifdef _WIN32
	fseek (fp,offset + OFFSET,SEEK_SET);
	readn = fread ((void *)buffer,1,size,fp);
#else
    readn = read_page (OFFSET + offset,buffer,size);
#endif

	return (readn);
}

int ll_write_bytes (int offset,const unsigned char * buffer,int size)
{
	int written = 0;

#ifdef _WIN32
	fseek (fp,offset + OFFSET,SEEK_SET);
	written = fwrite ((const void *) buffer,1,size,fp);
#else
    written = write_page (OFFSET + offset,(unsigned char *) buffer,size);
#endif

	return (written);
}

#if _WIN32
int erase_img (void) 
{
	if (fp) {		
		const int cnt = 4096;
		int i = 0;
		unsigned char *buf = (unsigned char *) MALLOC (cnt) ;
		
		memset ((void *) buf,0xff,cnt);
		
		for (i = 0 ;i < (8 << 8) ; i ++) 
			fwrite ((const void *) buf,cnt,1,fp);
		
		fseek (fp,0,SEEK_SET);

		FREE (buf);
		buf = NULL;
		
	} else 
		PANIC ("Disk.img is busy..");
	
	return (0);
}
#endif

int ll_ioctl (int cmd,unsigned char *buffer,int buffersz)
{
	switch (cmd) {
	case SMIOCTL_ERASE_FS:
		{
#if		_WIN32
			erase_img ();
#else
            NOR_EraseChip ();
#endif
		}
		break;

	case SMIOCTL_ERASE_PAGE:
		{
#ifdef _WIN32
			/* erase page respond */
			unsigned char *buf = (unsigned char *) MALLOC (fsinfo->sblk.page_size);

			memset ((void *) buf,0xff,fsinfo->sblk.page_size);
			ll_write_bytes (fsinfo->sblk.page_size * ((unsigned) buffer),buf,fsinfo->sblk.page_size);

			FREE (buf);
			buf = NULL;
#else
            erase_page (OFFSET + fsinfo->sblk.page_size * ((unsigned) buffer));
#endif
		}
		XDEBUG (0,"Erase Page %d",(unsigned) buffer);
		break;
	}
	return (0);
}

#if !_WIN32
/* message box macros */
#define MB_OK                   (0x00)
#define MB_NO                   (0x01)
#define MB_CANCEL               (-1)
#define MB_POWER_OFF            (-2)
#define MB_MAIN_KEY             (-3)

#define MBT_YES_ONLY            (0x02)
#define MBT_YESNO               (0x00)
#define MBT_YESNOCANCEL         (0x01)

#define MSGBOX_NORMAL           (0x00)              /* run as normal function */
#define MSGBOX_TASK             (0x01)              /* run as separate task */
extern int msg_box (const char *desc,int src_id,unsigned char ctl_style,unsigned char bstyle);
#endif

#if _DEBUG
char		buf [64 + 1] = {0};
#else
char		buf [64 + 1] = {0};
#endif

void PANIC (const char *fmt,...) 
{
	va_list		va_p;
	
    int         len = 0;
    int         xlen  = 0;
    char        *p = NULL;
	
    buf [0] = 0x00;
    
	strcat (buf,"(PANIC) # ");
	
	xlen = strlen (buf);
	p = buf + xlen;
	
	va_start (va_p,fmt);
	len = vsprintf ((char *) p,fmt,va_p);
	va_end (va_p);
	
	/* text out */
#if _WIN32
	printf ("%s\n",buf);
#else
	msg_box ((const char *) buf,-1,MSGBOX_NORMAL,MBT_YES_ONLY);
#endif

	/* panic !!!!!!!! */
	ll_ioctl (SMIOCTL_ERASE_PAGE,(unsigned char *) 0,0);
	
	while (1) ;
}

void	XDEBUG (int level,const char *fmt,...) 
{
#if _DEBUG
	va_list		va_p ;
    int		    xlen = 0,len = 0;
    char	    *p = NULL;
	
    buf [0] = 0x00;
	switch (level) {
	case 0:					/* log level */
		strcat (buf,"(DBG ) # ");			
		break;
		
	case 1:					/* warning level */
		strcat (buf,"(WARN) # ");
		break;

	case 2:
		strcat (buf,"(LOG ) # ");
	}
	
	xlen = strlen (buf);
	p = buf + xlen;
	
	va_start (va_p,fmt);
	len = vsprintf ((char *) p,fmt,va_p);
	va_end (va_p);
	
	/* text out */
	printf ("%s\n",buf);
#endif
}

int		HASHKEY (const char *string) 
{
	int i = 0 ;
	unsigned long  hash = 0; 

	for (i = 0 ; string [i] ; i ++) {
		hash = ((hash <<5) + hash) + (unsigned long) string [i]; 
	} 

	return (hash);
}
