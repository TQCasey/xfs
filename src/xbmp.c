#include "smfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "private.h"

/* to operate a bmp info cache
 *							2015/9/10
 *							 casey
 */


/* alloc a page from index bytes */
static int palloc (int i)
{
	/* used page bitmap mask */
	const unsigned char flg [] = {
		0x01,0x02,0x04,0x08,
		0x10,0x20,0x40,0x80,
	};

	struct _pagehdr	pagehdr = {0};
    int				j = 0,pagenr = 0;

	/* The trick :
	 * nor flash : 1 change to 0 need not to erase this page but 0 to 1 does 
	 * so we read-through device and check if this bit is 0 to 1 ,if positive 
	 * dirt_cnt ++ ,if negative write-through 
	 */
	if (!fsinfo->bmap [i])
		return (-1);
	
	for (j = 0 ; j < 8 ; j ++) {
		if ((fsinfo->bmap [i] & flg [j])) {
			fsinfo->bmap [i] &= ~(1 << j);
			pagenr = fsinfo->sblk.start_page + (i << 3) + j;
			XDEBUG (0,"Alloc page nr = %d",pagenr);
			
			/* read the page hdr first ,if it is used flag ,then clean it all */
			ll_read_bytes (pagenr * fsinfo->sblk.page_size,(unsigned char *) &pagehdr,sizeof (pagehdr));
			if (pagehdr.magic == USED_FLAG) {
				ll_ioctl (SMIOCTL_ERASE_PAGE,(unsigned char *) pagenr,0);
				/* page becomes clean */
			}

			pagehdr.magic = USED_FLAG;
			pagehdr.next  = -1L;
			fsinfo->nr_pages ++;
			
			/* write page hdr to device right now */
			ll_write_bytes (pagenr * fsinfo->sblk.page_size,(unsigned char *) &pagehdr,sizeof (pagehdr));
			return (pagenr);
		}
	}
	return (-1);
}

/* free a page */
static void pfree (int ipage)
{
    unsigned char b; 
    
	if (ipage < fsinfo->sblk.start_page) 
		XDEBUG (1,"Free sys-page !");
	
	XDEBUG (0,"Free Page = %d",ipage);
	
	ipage -= fsinfo->sblk.start_page;
	b = fsinfo->bmap [ipage >> 3] ;
	
	if (((~b) & (1 << (ipage & 0x07)))) {
		/* reset bitmap */
		fsinfo->bmap [ipage >> 3] |= (1 << (ipage & 0x07));

		fsinfo->nr_pages --;
		
		/* clear flash trunk */
		ll_ioctl (SMIOCTL_ERASE_PAGE,(unsigned char *) (ipage + fsinfo->sblk.start_page),0);
	} else {
		
		/* panic */
		XDEBUG (1,"Free free page !");
	}
}

/* get free page number index 
 * if no more page return -1 
 * if yes ,return the page number index 
 */
int alloc_page (void)
{
	int		i		= 0,pagenr = -1;
	int		bytes   = (fsinfo->sblk.page_nr >> 3) ;
	
	/* find a free page from bitmap ,searching with reverse order .... */
	for (i = 0 ; i < bytes ; i ++) {
		if ((pagenr = palloc (i)) >= 0)
			break;
	}
	return (pagenr);
}

/* free page number index 
 * free page to the bmap
 */
void free_page (int ipage)
{
	pfree (ipage);
}
