#include "smfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "private.h"

/* to collect info for smfs like bmp and file info if we have 
 *										2015/8/10  casey
 */


/* scan device to load bmp info and fill file info 
 */
int load_sysinfo (void) 
{
	int					i = 0,fileidx = 0;
	struct _pagehdr     pagehdr;
	//struct	_devfile	filehdr;
	
	/* get bmap bytes */
	int bytes = (fsinfo->sblk.page_nr >> 3) ;

	/* get max file */
	int max_files  = fsinfo->sblk.dev_files_nr;
	
	/* alloc bmap */
	fsinfo->bmap = (unsigned char *) MALLOC (bytes);
	
	if (!fsinfo->bmap) 
		PANIC ("Alloc bmap failed!");

	/* init as 0xff */
	memset ((void *) fsinfo->bmap,0xff,bytes);
	
	/* alloc file table */
	fsinfo->filetbl = (unsigned short *) MALLOC (sizeof (unsigned short) * max_files);

	if (!fsinfo->filetbl)
		PANIC ("Alloc File Table failed");

	/* init as 0x00 */
	memset ((void *) fsinfo->filetbl,0x00,sizeof (unsigned short) * max_files);

	/* scan device */
	for (i = 0 ; i < (int) fsinfo->sblk.page_nr ; i ++) {
		ll_read_bytes ((fsinfo->sblk.start_page + i) * fsinfo->sblk.page_size,
						(unsigned char *) &pagehdr,
						sizeof (pagehdr));

		/* if it is used page */
		if (pagehdr.magic == USED_FLAG) {

			/* build bmp info */
			fsinfo->bmap [i >> 3] &= ~(1 << (i & 0x07));	
			fsinfo->nr_pages ++;
			
			/* if it is file page */
			if (pagehdr.next == ~USED_FLAG) {
                /* when we reached here ,check_fs () completes a filesystem checking 
                 * we assume the filehdr magic work here ,so we need not check again .
                 */
#if     0
				ll_read_bytes ((fsinfo->sblk.start_page + i) * fsinfo->sblk.page_size + FILE_HDR_PAGEHDRSIZE,
								(unsigned char *) &filehdr,
								sizeof (filehdr));
                                

                /* additional file check here (not needed anymore : */
                                
                /* your code */
#endif

				if (fileidx >= (int) fsinfo->sblk.page_nr)
					continue;

				fsinfo->filetbl [fileidx ++] = fsinfo->sblk.start_page + i;
			}
		}
	}
	fsinfo->nr_files = fileidx;
	XDEBUG (0,"Total %d files,%d used pages ",fileidx,fsinfo->nr_pages);
	return (0);
}