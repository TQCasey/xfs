#include "smfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "private.h"

/* to check and repair the file system if there is something wrong 
 * with this smfs 
 *												2015/9/10 casey 
 */

extern int			put_file_hdr (int pagenr,struct _devfile *filehdr) ;

/* to check if it is a file header page */
static int is_filehdr_page (int pagenr) 
{
	struct _pagehdr		pagehdr;

	/* first to check if it is real file header page */
	ll_read_bytes   (pagenr * fsinfo->sblk.page_size,(unsigned char *) &pagehdr,sizeof (pagehdr));
	if (pagehdr.magic != USED_FLAG || pagehdr.next != ~USED_FLAG)  {
		/* nothing to do but erase this page  */

		if (pagehdr.magic == USED_FLAG) {
			/* erase the file header page */
			ll_ioctl (SMIOCTL_ERASE_PAGE,(unsigned char *) pagenr,0);
		}
		
		return (0);	
	}

	return (1);
}

/* get next area object */
static int get_safe_next (int entry)
{
	struct _pagehdr	pagehdr;

	if (entry < 0) 
		return (-2);

	read_page_bytes (entry,0,(unsigned char *) &pagehdr,sizeof (pagehdr));
	if (pagehdr.magic != USED_FLAG) {
		/* if this page is not used , then the next is invalidate 
		 * 1.this situation will never appear in new file mode (NEW_FILE)
		 * 2....
		 */
		return (-2);
	}
	return (pagehdr.next);
}

/* truncate the body of file with reverse order */
int safe_trunc_file_body (int filehdr_next_page,int buffered)
{
	int					entry	= filehdr_next_page;
	short				*ppage_nr= NULL;
	int					i = 0,j = 0,pages = 0;

	/* get pages first */
	while (entry > 0) {
		pages ++;
		entry = get_safe_next (entry);
		if (entry == -2) {
			pages --;
			break;
		}
	}

	XDEBUG (0,"Total pages %d",pages);
	if (pages <= 0)
		return (0);
	
	ppage_nr = (short *) MALLOC (pages * sizeof (short));
	if (!ppage_nr)
		PANIC ("Alloc pages array failed !",__FILE__);	/* maybe failed */
	
	entry	= filehdr_next_page;
	i		= 0;

	while (entry > 0) {
		/* erase next page */
		ppage_nr [i ++] = entry;
		entry = get_safe_next (entry);	/* file body next page (DON'T check the file size) */
		if (entry == -2) {
			i --;
			break;
		}
	}
	
	/* pop and free */
	for (j = i - 1 ; j >= 0 ; j --) {
		if (buffered)
			free_page (ppage_nr [j]);
		else
			ll_ioctl (SMIOCTL_ERASE_PAGE,(unsigned char *)ppage_nr [j],0);
	}
	
	FREE (ppage_nr);
	ppage_nr = NULL;
	
	return (0);
}

/* get file body size */
static int get_file_body_size (int next)
{
	int		pagenr = next;
	int		filesz = 0;

	while (pagenr != -1L) {
		if ((pagenr = get_safe_next (pagenr)) == -2) {	/* this pagenr is null page */
			break;
		}
		filesz += (fsinfo->sblk.page_size - FILE_BODY_PAGEHDRSIZE);
	}
	return (filesz);
}

/* clear pending state */
static void  clear_pending (int index,struct _devlog *plog)
{
	int				offset = fsinfo->sblk.page_size * fsinfo->sblk.log_page;
	
	plog->opt_pending = 0;
	ll_write_bytes (offset + index * sizeof (*plog),(unsigned char *) plog,sizeof (*plog));
}

/* continue truncate file pages */
static void continue_trunc_file_pages (int filehdr_page,struct _devfile *filehdr) 
{
	safe_trunc_file_body (filehdr->next_page_nr,0);
	if (is_filehdr_page (filehdr_page))
		ll_ioctl (SMIOCTL_ERASE_PAGE,(unsigned char *) filehdr_page,0);
}

static void rebuild_filehdr (struct _devlog *plog) 
{
	int					pagenr		= plog->param1;
	int					nextpagenr	= plog->param2;
	struct _devfile		filehdr		= {0};
	int					filesz		= 0;

	if (!is_filehdr_page (pagenr)) 
		return;

	/* read file header from device */
	ll_read_bytes	(pagenr * fsinfo->sblk.page_size + FILE_HDR_PAGEHDRSIZE,
					(unsigned char *) &filehdr,sizeof (filehdr));
	
	if ((unsigned char) filehdr.fname [0] == 0xff) {		/* filename is null ,erase it */
		/* file truncate is ending,but file writing is pending ...
		 * we need drop this page 
		 */
		XDEBUG (0,"No file hdr,need wipe all the file pages");	

		/* truncate all the file body pages */
		safe_trunc_file_body (nextpagenr,0);

		/* erase the file header page */
		ll_ioctl (SMIOCTL_ERASE_PAGE,(unsigned char *) pagenr,0);

	} else {
		/* if we still have this filename ,it means we need rebuild file size 
		 * (the relative-real file size,I means sometimes it may be bigger 
		  * than the real file size,but it's OK for then ....)
		 */
		XDEBUG (0,"FileName : %s,FileSize : %d",filehdr.fname,filehdr.filesz); 
		if (filehdr.filesz == -1L) {	
			/* file header size */
			filesz	= fsinfo->sblk.page_size - FILE_BODY_OFFSET;
			
			/* plus the file body size */
			filesz += get_file_body_size (filehdr.next_page_nr);
			
			/* file size if null,rebuild file size */
			filehdr.filesz = filesz;
			
			/* refresh the file header (file size) */
			put_file_hdr (pagenr,&filehdr);
		}
	}
}

/* handle with pending new file */
static void handle_new_file (struct _devlog *plog) 
{
	XDEBUG (0,"Handle New File pending....");
	rebuild_filehdr (plog);
}

/* handle with truncate file */
static void handle_truncate_file (struct _devlog *plog) 
{
	int					pagenr		= plog->param1;
	int					nextpagenr	= plog->param2;
	struct _devfile		filehdr		= {0};
	int					filesz		= 0;
	
	XDEBUG (0,"Handle Truncate File pending....");

	if (!is_filehdr_page (pagenr)) 
		return;

	ll_read_bytes	(pagenr * fsinfo->sblk.page_size + FILE_HDR_PAGEHDRSIZE,
					(unsigned char *) &filehdr,sizeof (filehdr));

	if ((unsigned char ) filehdr.fname [0] == 0xff) {
		/* file truncate is ending,but file writing is pending ...
		 * we need drop this page 
		 */
		XDEBUG (0,"No file hdr");	
		ll_ioctl (SMIOCTL_ERASE_PAGE,(unsigned char *) pagenr,0);
	} else {
		/* continue to truncate file pages if pending */
		XDEBUG (0,"Has File Header,continue to truncate file");

		/* truncate the file pages next */
		continue_trunc_file_pages (pagenr,&filehdr);
	}
}

/* handle overwrite file page */
static void handle_overwrite_page (struct _devlog *plog) 
{
	XDEBUG (0,"Handle Overwrite file pending...");
	rebuild_filehdr (plog);
}

/* handle append file pages */
static void handle_append_file (struct _devlog *plog)
{
	XDEBUG (0,"Handle Append file pending...");
	rebuild_filehdr (plog);
}

/* handle rewrite file page */
static void handle_rewrite_page (struct _devlog *plog) 
{
	int					pagenr		= plog->param1;
	int					nextpagenr	= plog->param2;
	struct _devfile		filehdr		= {0};
	int					filesz		= 0;

	XDEBUG (0,"Handle rewrite page pending...");
	if (!is_filehdr_page (pagenr)) 
		return;

	ll_read_bytes	(pagenr * fsinfo->sblk.page_size + FILE_HDR_PAGEHDRSIZE,
					(unsigned char *) &filehdr,sizeof (filehdr));
	if ((unsigned char) filehdr.fname [0] == 0xff) {
		/* if filename is null ,it means we lost filehdr page 
		 * so we need clear all the files pages left 
		 */
		safe_trunc_file_body (nextpagenr,0);

		/* erase file header */
		ll_ioctl (SMIOCTL_ERASE_PAGE,(unsigned char *) pagenr,0);

	} else {

		/* if filename exists,it means we lost one file body page, 
		 * we need clear all the pages which after this lost page 
		 */
		safe_trunc_file_body (nextpagenr,0);

		/* we need rebuild file size file header size 
		 */
		filesz	= fsinfo->sblk.page_size - FILE_BODY_OFFSET;
		
		/* plus the file body size */
		filesz += get_file_body_size (filehdr.next_page_nr);

		put_file_hdr (pagenr,&filehdr);
	}
}

/* check file system and try to repair the file system */
int check_fs (void) 
{
	struct _devlog	log ;
	int				n = (fsinfo->sblk.start_page - fsinfo->sblk.log_page) * fsinfo->sblk.page_size / sizeof (log);
	int				i = 0;
	int				offset = fsinfo->sblk.page_size * fsinfo->sblk.log_page;
	int				validcnt = 0,validindex = -1;

	/* init log system */
	init_log ();

	/* first to check the operation in the last log
	 * if is done before alloc a new log record
	 */
	for (i = n - 1,fsinfo->log_idx = -1 ; i >= 0 ; i --) {
		ll_read_bytes (offset + i * sizeof (log),(unsigned char *) &log,sizeof (log));
		if (log.cmd != LOG_NULL) {
			/* get the first validate log index...
			 * then the next is the first free log 
			 */
			if (!validcnt) {
				validindex = i;					
				validcnt ++;					
			}

			if (!log.opt_pending)
				continue;

			switch (log.cmd) {

			case LOG_NEW_FILE:				/* handler new file operation */
				handle_new_file (&log);
				clear_pending (i,&log);
				break;

			case LOG_TRUNC_FILE:
				handle_truncate_file (&log);
				clear_pending (i,&log);
				break;

			case LOG_OVERWRITE:
				handle_overwrite_page (&log);
				clear_pending (i,&log);
				break;

			case LOG_REWRITE:
				handle_rewrite_page (&log);
				clear_pending (i,&log);
				break;

			case LOG_APPEND:
				handle_append_file (&log);
				clear_pending (i,&log);
				break;

			default:
				PANIC ("Corrupt FileSystem log");
				break;
			}
		}
	}

	/* validate index */
	XDEBUG (0,"Validate Log Index : %d",validindex);
	fsinfo->log_idx = validindex;
	return (0);
}