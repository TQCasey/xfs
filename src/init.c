#include "smfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "private.h"


/* to init file system in a correct way to make it work out right *
 *
 *												2015/8/20 casey 
 */


/* file system init */
int smfs_init (void)
{
	int i = 0 ;

    if (fsinfo) {
        XDEBUG (1,"Prev fsinfo is not freed");
        return (-SMERR_PERM);
    }
	
	/* alloc memory and clear all */
	fsinfo = (struct _fsinfo *) MALLOC (sizeof(struct _fsinfo)) ;
	memset ((void *) fsinfo,0,sizeof (*fsinfo));
	
	/* first get the hardware information from device */
	if (hwl_init (&fsinfo->sblk)) {
        smfs_uninit ();
		XDEBUG (1,"Get hardware information failed.");
		return (-SMERR_IO);
	}
	
	/* then check if there is smfs is setup */
	ll_read_bytes (0,(unsigned char *) &fsinfo->sblk,sizeof (fsinfo->sblk));		/* read super block */
    if (fsinfo->sblk.magic != SMFS_MAGIC) {
		XDEBUG (0,"No SMFS FOUND");
        return (-SMERR_NOFS);
    }
	
	/* check fs if need  */
	check_fs ();

	/* load system files */
	if (load_sysinfo ())
		return (-SMERR_NOFS);

	/* clear open file tables */ 
	memset ((void *) fsinfo->mfiles,0,sizeof (fsinfo->mfiles));

	for (i = 0 ; i < NR_FILE ; i ++) {
		fsinfo->mfiles [i].open_log.log_idx = -1;
		fsinfo->mfiles [i].open_log.cmd		= LOG_NULL;

		fsinfo->mfiles [i].opt_log.log_idx	= -1;
		fsinfo->mfiles [i].opt_log.cmd		= LOG_NULL;
	}
	
	/* it is inited */
	fsinfo->inited	= 1;
	
	XDEBUG (0,"SMFS Inited OK");
	return (-SMERR_OK);
}

int smfs_uninit (void) 
{
	/* check if there any opened file(s) 
	 * if there is ,close it 
	 */
	int fd = 0;
	
	if (!fsinfo)
		return (-SMERR_OK);
	
	if (fsinfo->inited) {
		for (; fd < NR_FILE ; fd ++) {
			if (fsinfo->mfiles [fd].valid) 
				smfs_close (fd,0);
		}
	}
	
	/* free bmap and unit hardware */
	hwl_uninit (fsinfo);
	
	if (fsinfo->bmap)
		FREE ((void *) fsinfo->bmap);
	fsinfo->bmap = NULL;

	if (fsinfo->filetbl)
		FREE ((void *) fsinfo->filetbl);
	fsinfo->filetbl = NULL;

    if (fsinfo)
        FREE ((void *)  fsinfo);
	fsinfo = NULL;
    
    XDEBUG (0,"SMFS Uninit OK ");
	
	return (-SMERR_OK);
}
