#include "smfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "private.h"

/* to make a smfs on a norflash 
 *						2015/8/10
 *						casey
 */

int smfs_mkfs (void *arglist) 
{
	struct _sblk		sblk;
	
	if (hwl_init (&sblk)) {
		XDEBUG (1,"Get hardware information failed.");
		return (-SMERR_IO);
	}
	
	/* erase fs */
	ll_ioctl (SMIOCTL_ERASE_FS,0,0);
	
	/* super block page number = 0 */
	// ...

	/* log backup page number = 4 */
	sblk.log_backup_page = 4;
	
	/* log page number = 5 - 7 */
	sblk.log_page = 5;
	
	/* start from page number = 8 */
	sblk.start_page = 8;
	
	/* rid the file hash table and page bitmap */
	sblk.page_nr -= sblk.start_page;
	
	/* align page nr */
	sblk.page_nr = (sblk.page_nr >> 3) << 3;

	/* max dev files */
	sblk.dev_files_nr = NR_DEV_FILE;
	
	/* set magic */
	sblk.magic	= SMFS_MAGIC;
	
	ll_write_bytes (0,(const unsigned char *) &sblk,sizeof (sblk));

	hwl_uninit (0);
	return (-SMERR_OK);
}
