#include "smfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "private.h"


/* this is SiMple FileSystem ,(smfs) 
 * version			v3.0 
 *					casey	2015/9/21 
 *
 */

extern int			put_file_hdr (int pagenr,struct _devfile *filehdr) ;
/*
 * global values 
 */
struct _fsinfo			*fsinfo = NULL;

/* init fs if fs is not inited 
 */
static void check_fs_init (void)
{
	if (!fsinfo || !fsinfo->inited)
		smfs_init ();
}

/* get free page  numbers 
 */
static int get_free_pages (void) 
{
	int pages = fsinfo->sblk.page_nr - fsinfo->nr_pages;
	XDEBUG (0,"Pages Free %d/%d",pages,fsinfo->sblk.page_nr);
	return (pages);
}

static void regroup_filehdr (int fd,unsigned char *pagebuf)
{
	/* clear the file size slots */
	struct _devfile *phdr = NULL;
	struct _pagehdr	*pagehdr;
	int				next = -1;
	
	/* set next file link as 0xff */
	memset ((void *) (pagebuf + FILE_HDR_PAGEHDRSIZE - FILE_LINK_AREA_SIZE),0xff,FILE_LINK_AREA_SIZE);
	
	/* reset file link */
	pagehdr = (struct _pagehdr *) (pagebuf);
	pagehdr->magic	= USED_FLAG;
	if ((next = get_next (fsinfo->mfiles [fd].filehdr_page_nr)) < 0)
		next = -1L;
	pagehdr->next	= next;
		
	/* set filehdr filesz slot as 0xff */
	memset ((void *) (pagebuf + FILE_BODY_OFFSET - FILE_SIZE_AREA_SIZE),0xFF,FILE_SIZE_AREA_SIZE);
	
	/* set phdr */
	phdr = (struct _devfile *) (pagebuf + FILE_HDR_PAGEHDRSIZE);
	
	/* set the file size (mark it as dirt) */
	write_page_bytes (fsinfo->mfiles [fd].filehdr_page_nr,FILE_HDR_PAGEHDRSIZE,
					(const unsigned char *)  (&fsinfo->mfiles [fd]),
					sizeof (struct _devfile));
	
	/* reset file size */
	phdr->filesz = fsinfo->mfiles [fd].filesz;
}

/* alloc a page buffer for page 
 */
static void* alloc_pagebuf (int fd,int pagenr) 
{
	int i = 0 ;
	
	if (pagenr < fsinfo->sblk.start_page || fd < 0 || fd >= NR_FILE)
		return (0);
	 
	/* first find in page buffer tables */
	for (i = 0 ; i < NR_PAGEBUF ; i ++) {
		if (fsinfo->pagebufs [i].valid) {
			if (pagenr == fsinfo->pagebufs [i].pagenr && fsinfo->pagebufs [i].pagebuf) {
				XDEBUG (1,"Page nr %d is already buffered!",pagenr);
				return (NULL);
			}
		}
	}
	
	/* if not in page buffers ,alloc a new one */  
	for (i = 0 ; i < NR_PAGEBUF ; i ++) {
		if (!fsinfo->pagebufs [i].valid) {
			/* page buffer is not buffered ,try to alloc one */
			
			/* alloc memory failed */
			fsinfo->pagebufs [i].pagebuf	= (unsigned char *) MALLOC (fsinfo->sblk.page_size);
			if (!fsinfo->pagebufs [i].pagebuf)
				return (0);
			
			fsinfo->pagebufs [i].dirt		= 0;
			fsinfo->pagebufs [i].pagenr		= pagenr;
			fsinfo->pagebufs [i].fd			= fd;
			fsinfo->pagebufs [i].valid		= 1;
			
			/* read to buffer */
			ll_read_bytes (pagenr * fsinfo->sblk.page_size,(unsigned char *) fsinfo->pagebufs [i].pagebuf,fsinfo->sblk.page_size);
			fsinfo->pagebufs [i].dirt		= 0;
			
			XDEBUG (0,"Alloc page buffer for page %d ",pagenr);
			return (fsinfo->pagebufs [i].pagebuf);
		}
	}
	
	/* no more page buffer is free */
	return (0);
}

/* free page buffer 
 */
static int free_pagebuff (int fd) 
{
	int i = 0;
	
	for (i = 0 ; i < NR_PAGEBUF ; i ++) {
		if (!fsinfo->pagebufs [i].valid) 
			continue;
		
		if (fsinfo->pagebufs [i].fd == fd && fsinfo->pagebufs [i].dirt) {
			/* check if page buffer is OK */
			if (!fsinfo->pagebufs [i].pagebuf) {

				fsinfo->pagebufs [i].fd		= -1;
				fsinfo->pagebufs [i].dirt	= 0;
				fsinfo->pagebufs [i].valid	= 0;
				fsinfo->pagebufs [i].pagenr = -1;

				XDEBUG (1,"Sync page %d without pb!",fsinfo->pagebufs [i].pagenr);

				return (-1);
			}
			XDEBUG (0,"Free page buff for page %d",fsinfo->pagebufs [i].pagenr);
			
            if (!fsinfo->mfiles [fd].isnewpage) {		/* if it is not new file */
                /* erase old page */
                ll_ioctl (SMIOCTL_ERASE_PAGE,(unsigned char *) (unsigned) fsinfo->pagebufs [i].pagenr,0);
				fsinfo->mfiles [fd].isnewpage = 1;

				if (fsinfo->mfiles [fd].filehdr_page_nr == fsinfo->pagebufs [i].pagenr) {
					XDEBUG (0,"Regroup File size....");
					
					/* re-group the filehdr */
					regroup_filehdr (fd,fsinfo->pagebufs [i].pagebuf);
				}
            }
			
			/* write back */
			ll_write_bytes (fsinfo->pagebufs [i].pagenr * fsinfo->sblk.page_size,fsinfo->pagebufs [i].pagebuf,fsinfo->sblk.page_size);
			
			fsinfo->pagebufs [i].dirt	= 0;
			fsinfo->pagebufs [i].fd		= -1;
			fsinfo->pagebufs [i].pagenr	= -1;
			fsinfo->pagebufs [i].valid	= 0;
			
			FREE ((void *) fsinfo->pagebufs [i].pagebuf);
			fsinfo->pagebufs [i].pagebuf = NULL;
		}
	}
	return (0);
}

/* truncate file size */
static int trunc_file (int fd)
{
	int					entry	= fsinfo->mfiles [fd].filehdr_page_nr;
	struct _pagehdr		pagehdr;

	/* truncate file header */
	safe_trunc_file_body (fsinfo->mfiles [fd].next_page_nr,1);

    /* erase filehdr page */
    ll_ioctl (SMIOCTL_ERASE_PAGE,(unsigned char *) entry,0);
	
	/* proceed with fd table */
	fsinfo->mfiles [fd].filesz			= -1L;					
	fsinfo->mfiles [fd].next_page_nr    = -1L;
    fsinfo->mfiles [fd].ctime			= ll_ioctl (SMIOCTL_GETTIME,0,0);	/* get create time */

	pagehdr.magic = USED_FLAG;
	pagehdr.next  = ~USED_FLAG;						/* this is a file mark */
	write_page_bytes (entry,0,(const unsigned char *) &pagehdr,sizeof (pagehdr));
	
	/* write file header */
	write_page_bytes (entry,FILE_HDR_PAGEHDRSIZE,(const unsigned char *) &fsinfo->mfiles [fd],sizeof (struct _devfile));

	return (-SMERR_OK);
}

static int get_file_hdr (int pagenr,struct _devfile *filehdr)
{
	unsigned			filesz = 0;
	int					n = FILESIZE_MOD_NR;
	int					offset = 0,idx = 0,i = 0;

	read_page_bytes (pagenr,FILE_HDR_PAGEHDRSIZE,(unsigned char *) filehdr,sizeof (*filehdr));
    filehdr->fname [FNAMELEN] = 0x00;
	offset = FILE_BODY_OFFSET - FILE_SIZE_AREA_SIZE;

	/* why using this 'ugly' method ? (searching from front) 
	 * why not searching from near ? Because most of these files 
	 * in our device will not be appended ,these files just use 
	 * the normal file size in normal page 
	 */
	for (i = 0 ; i < n ; i ++) {
		/* read size from device */
		read_page_bytes (pagenr,offset + i * sizeof (filesz),(unsigned char *) &filesz,sizeof (filesz));

		if (filesz == 0xffffffffUL) {
			if (!i)
				return (0);
			else {
				/* read previous file size */
				read_page_bytes (pagenr,offset + (i - 1) * sizeof (filesz),(unsigned char *) &filesz,sizeof (filesz));
				filehdr->filesz = filesz;
				idx = i - 1;
			}
			XDEBUG (0,"Find validate file size slot %d",idx);
			return (0);
		}
	}
	/* last one file size slot */
	XDEBUG (0,"Last one file size slot");

	/* read previous file size */
	read_page_bytes (pagenr,offset + (i - 1) * sizeof (filesz),(unsigned char *) &filesz,sizeof (filesz));
	filehdr->filesz = filesz;
	return (0);
}

int put_file_hdr (int pagenr,struct _devfile *filehdr) 
{
	unsigned			filesz = 0;
	struct _devfile		devfile = {0};
	int					n = FILESIZE_MOD_NR;
	int					offset = 0,i = 0;

	/* read file header first */
	read_page_bytes (pagenr,FILE_HDR_PAGEHDRSIZE,(unsigned char *) &devfile,sizeof (devfile));
	XDEBUG (0,"Old Device filesz = %d",devfile.filesz); 

	/* check if need renew filesize (if size get bigger,then renew file size) */
	if (filehdr->next_page_nr != -1L && devfile.next_page_nr == -1L) {
		devfile.next_page_nr  = filehdr->next_page_nr;
	}
	
	if (devfile.filesz == -1L) {
		XDEBUG (0,"Pretty New File Header");
		write_page_bytes (pagenr,FILE_HDR_PAGEHDRSIZE,(const unsigned char *) filehdr,sizeof (struct _devfile));
		return (0);
	}
	offset = FILE_BODY_OFFSET - FILE_SIZE_AREA_SIZE;
	
	/* why using this 'ugly' method ? (search from front) 
	 * why not searching from near ? Because most of these files 
	 * in our device will not be appended ,these files just use 
	 * the normal file size in normal page 
	 */
	for (i = 0 ; i < n ; i ++) {
		/* read size from device */
		read_page_bytes (pagenr,offset + i * sizeof (filesz),(unsigned char *) &filesz,sizeof (filesz));
		if (filesz == -1L) {
			/* get old size from device */
			if (!i) {
				filesz = devfile.filesz;
			} else {
				/* read previous file size */
				read_page_bytes (pagenr,offset + (i - 1) * sizeof (filesz),(unsigned char *) &filesz,sizeof (filesz));
			}

			/* check if need renew file size */
			if (filesz == (unsigned) filehdr->filesz) {
				XDEBUG (0,"No need to change file size");
				return (0);
			}

			/* if need ,change file size */
			filesz = filehdr->filesz;
			write_page_bytes (pagenr,offset + i * sizeof (filesz),(const unsigned char *) &filesz,sizeof (filesz));
			XDEBUG (0,"Find free file size slot %d",i);
			return (0);
		}
	}
	XDEBUG (0,"No more free filesz DWORD");
	return (-1);
}

/* get next area object */
int get_next (int entry)
{
	struct _pagehdr	pagehdr;
	unsigned char	b = 0;
	int				pagenr = entry;

	if (entry < fsinfo->sblk.start_page) 
		return (-1);

	read_page_bytes (entry,0,(unsigned char *) &pagehdr,sizeof (pagehdr));
	if (pagehdr.magic != USED_FLAG) {
		/* first to check if if bmp cache bit is set 
		 * if yes ,erase it and return -1
		 * if not  return -1;
		 */
		entry -= fsinfo->sblk.start_page;
		b = fsinfo->bmap [entry >> 3] ;

		if (((~b) & (1 << (entry & 0x07)))) {	/* if this page is taken ,erase it */
			ll_ioctl (SMIOCTL_ERASE_PAGE,(unsigned char *) (pagenr),0);
			pagehdr.magic = USED_FLAG;
			pagehdr.next  = -1;
			write_page_bytes (pagenr,0,(const unsigned char *) &pagehdr,sizeof (pagehdr));
			return (-1);
		} 
		/* if not ,return -2 */
		return (-2);
	}
	return (pagehdr.next);
}

/* put next area object */
int put_next (int pagenr,int next)
{
	struct _pagehdr pagehdr;

	if (pagenr < fsinfo->sblk.start_page)
		return (-1);

	/* read the old page header */
	read_page_bytes (pagenr,0,(unsigned char *) &pagehdr,sizeof (pagehdr));

	/* pagehdr panic */
	if (pagehdr.magic != USED_FLAG || pagehdr.next != -1L) {
		ll_ioctl (SMIOCTL_ERASE_PAGE,(unsigned char *) (pagenr),0);
		pagehdr.magic = USED_FLAG;
	}

	/* if it is new next area ,put it */
	pagehdr.next = next;
	write_page_bytes (pagenr,0,(const unsigned char *) &pagehdr,sizeof (pagehdr));
	return (0);
}

/* open file */
int smfs_open (const char *filename,unsigned char mode,void *arglist)
{
	int					fd = -1;
	int					pagenr = -1,i = 0,index = 0;
    int                 key = 0;
    int                 entry = 0,next = 0;
	unsigned char		*pagebuf = NULL;
	struct _pagehdr		pagehdr;
	
	check_fs_init ();
	
	/* filename check */
	if (!filename) {
		XDEBUG (1,"Filename is null ");
		return (-SMERR_ARG);
	}

	/* truncate or create flags couples with write flag
	 * append must use write mode 
	 */
	if ((mode & (SMO_TRUNC | SMO_CREATE | SMO_APPEND)))
		mode |= SMO_WRITE;
	
	/* whatever the mode is, read or write ,we cannot not pass through
	 * when there is one write operation in fd table 
	 */
	for (i = 0 ; i < NR_FILE ; i ++) {
		if (!fsinfo->mfiles [fd].valid)
			continue;
		if (0 == strcmp (filename,fsinfo->mfiles [i].fname)) {
			/* yes ,we find it 
			 * check if there is someone writing this file 
			 */
			if (fsinfo->mfiles [i].mode & SMO_WRITE) {
				XDEBUG (0,"Multi-write is not allowed!");
				return (-SMERR_PERM) ;		/* no permission */
			}
		}
	}
	
	/* first ,alloc a fd */
	for (fd = 0; fd < NR_FILE ; fd ++) {
		if (!fsinfo->mfiles [fd].valid) {
			memset ((void *) &fsinfo->mfiles [fd],0,sizeof (fsinfo->mfiles [fd]));
			fsinfo->mfiles [fd].open_log.log_idx	= -1;
			fsinfo->mfiles [fd].open_log.cmd		= LOG_NULL;
			
			fsinfo->mfiles [fd].opt_log.log_idx		= -1;
			fsinfo->mfiles [fd].opt_log.cmd			= LOG_NULL;
			break;
		}
	}
	
	/* no more fd */
	if (fd == NR_FILE) {
		XDEBUG (1,"No more free fd");
		return (-SMERR_NOFD);
	}
	
	/* overwrite flag is used by smfs ONLY */
	mode &= ~SMO_OVERWRITE;
    
    /* check if file is existed */
    mode &= ~SMO_FILE_EXISTS;
    
    fsinfo->mfiles [fd].filepos			= 0;
	
	/* check if it already existed */
	if (0 != (pagenr = find_filehdr_pagenr (filename,&index))) {
        
		mode |= SMO_FILE_EXISTS;    /* file does existed */
        mode &= ~SMO_CREATE;        /* no more create */
		
		/* read filehdr from device */
		get_file_hdr (pagenr,(struct _devfile *) &fsinfo->mfiles [fd]);
		
		/* set filehdr pagenr */
		fsinfo->mfiles [fd].filehdr_page_nr = pagenr;
		
		/* if in write mode */
		if ((mode & (SMO_WRITE))) {
            /* it is overwrite mode */
            mode |= SMO_OVERWRITE;

			fsinfo->mfiles [fd].valid			= 1;
			fsinfo->mfiles [fd].cnt				= 1;
            
			if (mode & SMO_TRUNC) {
				/* for that we don't alloc page buffer for this file 
				 * so truncate file need not to free page buffer 
				 */

				/* set a log for open truncate mode */
				alloclog4open (fd,LOG_TRUNC_FILE,(unsigned short) fsinfo->mfiles [fd].filehdr_page_nr,
												 (unsigned short) fsinfo->mfiles [fd].next_page_nr);

				/* truncate file size to zero */
				trunc_file (fd);

				/* free this log */
				free_log (&fsinfo->mfiles [fd].open_log);

				/* set a log for fake new file mode */
				alloclog4open (fd,LOG_NEW_FILE,(unsigned short) fsinfo->mfiles [fd].filehdr_page_nr,
											   (unsigned short) fsinfo->mfiles [fd].next_page_nr);
				
                /* fake no file exists ,no overwrite ,no truncate */
                mode &= ~(SMO_TRUNC | SMO_OVERWRITE | SMO_FILE_EXISTS);

				/* fake new page */
				fsinfo->mfiles [fd].isnewpage = 1;

				/* sync position 0 */
				fsinfo->mfiles [fd].syncpos = 0;
                
				XDEBUG (0,"Truncate file size to 0");
			} else if (mode & SMO_APPEND) {
                
                /* no overwrite and append mode any more */
                mode &= ~(SMO_OVERWRITE /* | SMO_APPEND */) ;
                
                /* filepos adjust to the end of file */
                fsinfo->mfiles [fd].filepos = fsinfo->mfiles [fd].filesz;

				/* fake a new page */
				fsinfo->mfiles [fd].isnewpage  = 1;

				/* sync position to file end */
				fsinfo->mfiles [fd].syncpos = fsinfo->mfiles [fd].filesz;

				/* alloc a log for open append mode */
				alloclog4open (fd,LOG_APPEND,(unsigned short) fsinfo->mfiles [fd].filehdr_page_nr,
											 (unsigned short) fsinfo->mfiles [fd].next_page_nr);

				XDEBUG (0,"Append file content!");   
            } else {
				/* overwrite page : 
				 * this will not truncate page number link 
				 * just overwrite the old pages 
				 */
				mode |= SMO_OVERWRITE;

				/* not a new page */
				fsinfo->mfiles [fd].isnewpage  = 0;

				/* sync position to end */
				fsinfo->mfiles [fd].syncpos = fsinfo->mfiles [fd].filesz;

				XDEBUG (0,"Overwrite file content!");

				/* alloc a log for open overwrite mode */
				alloclog4open (fd,LOG_OVERWRITE,(unsigned short) fsinfo->mfiles [fd].filehdr_page_nr,
												(unsigned short) fsinfo->mfiles [fd].next_page_nr);
			}			
			/* fall through */			
			if (!alloc_pagebuf (fd,pagenr)) {
				XDEBUG (0,"Alloc page buffer failed ");
				return (-SMERR_OOM);
			}
			XDEBUG (0,"Alloc page buff for page %d",pagenr);
            
            fsinfo->mfiles [fd].dirt        = 1;
		} else {
            /* read mode */
			fsinfo->mfiles [fd].valid		= 1;
			fsinfo->mfiles [fd].cnt			= 1;
            fsinfo->mfiles [fd].dirt        = 0;
        }
        
        /* set mode */
        fsinfo->mfiles [fd].mode			= mode;
		/* OK */
		return (fd);
	} else {
		/* if we can not find file,but append mode is set 
		 * that equals the SMO_CREATE flag *
		 */
        if (mode & SMO_APPEND)
            mode |= SMO_CREATE;
        
		if (!(mode & SMO_CREATE)) {
			XDEBUG (1,"No such file!");
			return (-SMERR_NOSRC);
		}
	}

    /* if new file ,no truncate and no append */
    mode        &= ~(/* SMO_APPEND |  */SMO_TRUNC);

	if ((index = find_free_fileindex ()) < 0) {
		XDEBUG (0,"more than Max files");
		return (-SMERR_NOSRC);
	}
	XDEBUG (0,"File table index = %d",index);

	/* set this fd as validate */    
	if ((pagenr = alloc_page ()) < 0) {
		XDEBUG (1,"No more free page ");
		return (-SMERR_NOPAGE);
	}
    
	XDEBUG (0,"FileHdr page nr = %d ",pagenr);
	/* full fill this struct */
	fsinfo->mfiles [fd].valid			= 1;
	fsinfo->mfiles [fd].dirt			= 1;
	fsinfo->mfiles [fd].cnt				= 1;
	fsinfo->mfiles [fd].next_page_nr	= -1L;								/* no next page nr */
	fsinfo->mfiles [fd].ctime			= ll_ioctl (SMIOCTL_GETTIME,0,0);	/* get create time */
	fsinfo->mfiles [fd].filesz			= 0xffffffffUL;						/* -1 filesz */
	fsinfo->mfiles [fd].mode			= mode;
	fsinfo->mfiles [fd].filehdr_page_nr	= pagenr;
	strcpy ((char *) fsinfo->mfiles [fd].fname,filename);					/* set file name */
	fsinfo->mfiles [fd].hashkey			= HASHKEY (filename);

	/* we don't link hash here ,instead we do that in 
	 * close operation 
	 */
	
	/* take mfiles */
	fsinfo->mfiles [fd].filehdr_page_nr	= pagenr;
	fsinfo->mfiles [fd].filepos			= 0;
	
	if (!(pagebuf = (unsigned char *) alloc_pagebuf (fd,pagenr))) {
		XDEBUG (0,"Alloc page buffer failed ");
		return (-SMERR_OOM);
	}

	fsinfo->mfiles [fd].isnewpage		= 1;		/* this filehdr page is new */
	fsinfo->mfiles [fd].syncpos			= 0;		/* default sync position 0 */

	/* set a log : start 
	 * alloc a log for open overwrite mode 
	 */
	alloclog4open (fd,LOG_NEW_FILE,(unsigned short) fsinfo->mfiles [fd].filehdr_page_nr,
								   (unsigned short) fsinfo->mfiles [fd].next_page_nr);

	/* add to file table */
	fsinfo->mfiles [fd].fileindex		= index;
	add_filehdr_pagenr (index,pagenr);

	pagehdr.magic = USED_FLAG;
	pagehdr.next  = ~USED_FLAG;						/* this is a file mark */
	write_page_bytes (pagenr,0,(const unsigned char *) &pagehdr,sizeof (pagehdr));

	/* write file header */
	write_page_bytes (pagenr,FILE_HDR_PAGEHDRSIZE,
					 (const unsigned char *) &fsinfo->mfiles [fd],
					 sizeof (struct _devfile));

	/* done */
	return (fd);
}

/* switch page buffer to page number 
 * return the sync bytes 
 */
static int switch_pagebuff (int fd,int to_pagenr)
{
	int		i = 0,cnt = 0,next = 0;
	
	/* if not write mode ,return */
	if (!(fsinfo->mfiles [fd].mode & SMO_WRITE))
		return (cnt);
	
	/* find page buffer in page buffers table */
	for (i = 0 ; i < NR_PAGEBUF ;i ++) {
		if (!fsinfo->pagebufs [i].valid) 
			continue;
		if (fsinfo->pagebufs [i].fd == fd && fsinfo->pagebufs [i].pagebuf) {
			/* we find it */
			break;
		}
	}
	
	/* check if page buffer is ready ? */
	if (i == NR_PAGEBUF) {
		XDEBUG (0,"No page buff!");
		return (cnt);
	}
	
	/* check if the page buff is changed */
	if (to_pagenr == fsinfo->pagebufs [i].pagenr) {
		XDEBUG (0,"No need to switch page buffer !");
		return (cnt);
	}
	
	XDEBUG (0,"Switch page buffer from %d to %d",fsinfo->pagebufs [i].pagenr,to_pagenr);
	
	/* cross page : sync previous page buff */
	if (fsinfo->pagebufs [i].dirt) {
		
		/* because we can not allow multi-write operation ,
		 * so that using ll_write_page is safe
		 */
		XDEBUG (0,"Sync page buffer %d",fsinfo->pagebufs [i].pagenr);
		
		/* erase old page */
        if (!fsinfo->mfiles [fd].isnewpage) {
			/* alloc a new log for operation */

			/* get next page nr */
			if ((next = get_next (fsinfo->mfiles [fd].filehdr_page_nr)) < 0)
				next = -1;

			/* get its next page number */
			alloclog4opt (fd,LOG_REWRITE,(unsigned short) fsinfo->mfiles [fd].filehdr_page_nr,(unsigned short) next);

            /* only need overwrite need erase whole page */
            ll_ioctl (SMIOCTL_ERASE_PAGE,(unsigned char *) ((unsigned)fsinfo->pagebufs [i].pagenr),0);

			/* check if it is filehdr page */
			if (fsinfo->pagebufs [i].pagenr == fsinfo->mfiles [fd].filehdr_page_nr) {
				XDEBUG (0,"File Header Page is changed,we'll regroup it.");
				regroup_filehdr (fd,fsinfo->pagebufs [i].pagebuf);
			}
        }
        
		/* get the sync bytes count */
        if (ll_write_bytes (fsinfo->pagebufs [i].pagenr * fsinfo->sblk.page_size,fsinfo->pagebufs [i].pagebuf,fsinfo->sblk.page_size))
			cnt = fsinfo->sblk.page_size;

		fsinfo->pagebufs [i].dirt  = 0;

		/* clear state */
		if (!fsinfo->mfiles [fd].isnewpage) {
			fsinfo->mfiles [fd].isnewpage = 1;

			/* erase operation is done */
			free_log (&fsinfo->mfiles [fd].opt_log);
		}
	}
	/* anyway ,buffer switching will reset the dirt flag and 
	 * switch buffer event the buffer isn't dirt (no need sync) 
	 */
	
	/* load current page to page buff */
	ll_read_bytes (to_pagenr * fsinfo->sblk.page_size,fsinfo->pagebufs [i].pagebuf,fsinfo->sblk.page_size);
	
	/* clean */
	fsinfo->pagebufs [i].dirt = 0;
	
	/* set another page */
	fsinfo->pagebufs [i].pagenr = to_pagenr;

	/* return sync cnt */
	return (cnt);
}

/* get current page number and current page-in-offset from filepos 
 */
static void get_page_param (int fd,int *curpage,int *curoff) 
{
	struct _devfile				filehdr = {0};
	const int					filebody_pgsz	= fsinfo->sblk.page_size - FILE_BODY_PAGEHDRSIZE;
	const int					filehdr_pgsz	= fsinfo->sblk.page_size - FILE_BODY_OFFSET;
	int							filepos	= fsinfo->mfiles [fd].filepos,entry = 0;
	
	/* to find where current filepos locate at
	 * which page number and which page in offset
	 */
	if (filepos < filehdr_pgsz) {
		*curpage		= fsinfo->mfiles [fd].filehdr_page_nr;
		*curoff			= filepos + FILE_BODY_OFFSET;
	} else {
		filepos			-= filehdr_pgsz;	/* sub filehdr */
		*curpage		= fsinfo->mfiles [fd].next_page_nr;
		
		if (*curpage < 0) {
			/* if file size is -1L ,but the next page is -1L 
			 * we adjust the *curpage and *curoff as fileheader 
			 * page and offset ....
			 */
			*curpage = fsinfo->mfiles [fd].filehdr_page_nr;
			*curoff  = FILE_BODY_OFFSET ;

			return;
		}

		entry = *curpage;
		
		while (entry != -1 && filepos >= filebody_pgsz) {
			if ((entry = get_next (*curpage)) < 0)	/* file header body */
				break;
			filepos -= filebody_pgsz;
			*curpage = entry;
		}
		*curoff = (filepos + sizeof (struct _pagehdr)) /*& 0xfff*/;
	}
}

/* write file */
int smfs_write (int fd,const void *buffer,int size,void *arglist)
{
	struct _devfile				filehdr = {0};
	int							written = 0;
	int							curpage = 0;		/* current page number */
	int							curoff	= 0;		/* current page in offset */
	int							pagenr ,next;
	int							bytes = 0,left = size;
	unsigned char				*pbuffer = (unsigned char *) buffer;
	int							syncpos = 0;
    
    check_fs_init ();
	
    /* fd check */
	if (fd < 0 || fd >= NR_FILE || size < 0 || !buffer || !fsinfo->mfiles [fd].valid) 
		return  (-SMERR_ARG);
	
	/* is it writable? */
	if (!(fsinfo->mfiles [fd].mode & SMO_WRITE))
		return (-SMERR_ARG);
    
	/* get sync position */
	syncpos = fsinfo->mfiles [fd].syncpos;

	/* to find where current filepos locate at
	 * which page number and which page in offset
	 */
	get_page_param (fd,&curpage,&curoff);
	
	if ((bytes = fsinfo->sblk.page_size - curoff) >= size)
		bytes = size;
	
    left = size ;
    
	/* write file content 
	 */
	while (left > 0) {
		if (!bytes) {	/* cross page */
			/* check it need alloc page */
			if (curpage == fsinfo->mfiles [fd].filehdr_page_nr)
				next = fsinfo->mfiles [fd].next_page_nr;
			else 
				next = get_next (curpage);

			if (next == -2)			/* if curpage is null page ,break it */
				break;

			if (next == -1L) {
				/* if next page is null alloc a new page */
				if ((pagenr = alloc_page ()) < 0) {
					XDEBUG (1,"No more free page !");
					break;
				}
				if (curpage == fsinfo->mfiles [fd].filehdr_page_nr) {
					/* sync next page to page buffer immediately 
					 */
					fsinfo->mfiles [fd].next_page_nr	= pagenr;
					fsinfo->mfiles [fd].filesz			= -1L;
					write_page_bytes (curpage,FILE_HDR_PAGEHDRSIZE,
										(const unsigned char *) &fsinfo->mfiles [fd],
										sizeof (struct _devfile));
				} else {
					if (put_next (curpage,pagenr) < 0)
						break;
				}
				/* if it is new file content ,sync position inc 
				 * (indicate the real file position written in device) ...
				 */
				syncpos += switch_pagebuff (fd,pagenr);
				curpage	= pagenr;
				curoff	= FILE_BODY_PAGEHDRSIZE;

				/* this new page is new */
				fsinfo->mfiles [fd].isnewpage  = 1;
			} else {
				curpage = next;
				
				/* next page existed 
				 * switch page buffer to next page number 
				 */
				switch_pagebuff (fd,curpage);
				curoff	= FILE_BODY_PAGEHDRSIZE;

				/* this page is not new */
				fsinfo->mfiles [fd].isnewpage = 0;
			}
		}
		
		if ((bytes = (fsinfo->sblk.page_size - curoff)) >= left)
			bytes = left;
		
		write_page_bytes (curpage,curoff,pbuffer,bytes);
		
		pbuffer += bytes;
		left	-= bytes;
		curoff	+= bytes;
		written	+= bytes;
		
		fsinfo->mfiles [fd].filepos += bytes;
	}
    
	/* renew file size */
    if (fsinfo->mfiles [fd].filepos > fsinfo->mfiles [fd].filesz)
        fsinfo->mfiles [fd].filesz = fsinfo->mfiles [fd].filepos;

	/* renew sync pos */
	if (syncpos > fsinfo->mfiles [fd].syncpos)
		fsinfo->mfiles [fd].syncpos = syncpos;
	
	return (written);
}

/* read file */
int smfs_read (int fd,void *buffer,int size,void *arglist)
{
	int readn = 0;
	struct _devfile				filehdr = {0};
	int							curpage = 0;		/* current page number */
	int							curoff	= 0;		/* current page in offset */
	int							bytes = 0,left = size;
	unsigned char				*pbuffer = (unsigned char *) buffer;
	
	check_fs_init ();
	
	/* fd check */
	if (fd < 0 || fd >= NR_FILE || size <= 0 || !buffer || !fsinfo->mfiles [fd].valid) 
		return  (-SMERR_ARG);
	
	/* readable */
	if (!(fsinfo->mfiles [fd].mode & SMO_READ))
		return (-SMERR_ARG);
	
	/* to check if the read is out of range */
	if (fsinfo->mfiles [fd].filepos + size >= fsinfo->mfiles [fd].filesz) {
		size = fsinfo->mfiles [fd].filesz - fsinfo->mfiles [fd].filepos;
	}
	
	if (size <= 0)
		return (0);
	
	/* to find where current filepos locate at
	 * which page number and which page in offset
	 */
	get_page_param (fd,&curpage,&curoff);
	
	if ((bytes = fsinfo->sblk.page_size - curoff) >= size)
		bytes = size;
	
	while (left > 0) {
		if (!bytes) {	/* cross page */
			/* check it need alloc page */
			if (curpage == fsinfo->mfiles [fd].filehdr_page_nr)
				curpage = fsinfo->mfiles [fd].next_page_nr;
			else 
				curpage = get_next (curpage);	/* need not to check */
			
			if (curpage < 0) {
				/* file size may be wrong but we need continue ... */
				break;
			}
			
			curoff = FILE_BODY_PAGEHDRSIZE;
		}
		if ((bytes = (fsinfo->sblk.page_size - curoff)) >= left)
			bytes = left;
		
		read_page_bytes (curpage,curoff,pbuffer,bytes);
		
		pbuffer += bytes;
		left	-= bytes;
		curoff	+= bytes;
		readn	+= bytes;
		
		fsinfo->mfiles [fd].filepos += bytes;
	}
	return (readn);
}

/* remove file */
int smfs_unlink (const char *filename,void *arglist)
{
    int					fd = 0 ;
    struct _devfile		filehdr;
	int					i = 0,pagenr = 0,index = 0;
	struct _memlog		memlog;
	
	check_fs_init ();
    
    if (!filename)
        return (-SMERR_ARG);
   
	/* to check if it is in fd table ,for that we use flat fs
	 * so filename is unique at this time 
	 */
	for ( ; fd < NR_FILE ; fd ++) {
		if (fsinfo->mfiles [fd].valid && !strcmp ((const char *) fsinfo->mfiles [fd].fname,filename)) {
			XDEBUG (0,"File is busy.");
			return (-SMERR_PERM);		/* file is busy */
		}
	}

	if (!(pagenr = find_filehdr_pagenr (filename,&index))) {
		XDEBUG (0,"No such file");
		return (-SMERR_NOSRC);
	}
	
	/* get file header */
	get_file_hdr (pagenr,&filehdr);

	/* get a log for this unlink operation */
	alloc_log (&memlog,LOG_TRUNC_FILE,(unsigned short) pagenr,0xffff);

	/* first truncate file body */
	safe_trunc_file_body (filehdr.next_page_nr,1);

	/* erase file hdr page */
	free_page (pagenr);

	/* remove file header page number */
	remove_filehdr_pagenr (index);

	/* free this log */
	free_log (&memlog);
	
	return (-SMERR_OK);
}

int smfs_seek (int fd,int offset,int fromwhere,void *arglist)
{
	int		curpage = 0,curoff = 0;
	int		oldpage = 0,oldoff = 0;
	char	movback = 0;
	
	check_fs_init ();
	
	/* fd check */
	if (fd < 0 || fd >= NR_FILE || !fsinfo->mfiles [fd].valid) 
		return  (-SMERR_NOSRC);
	
	/* seek positions */
	switch (fromwhere) {
	case 0:				/* seek from start */
        curoff = offset;
		break;
		
	case 1:					/* seek from current position */
		curoff = fsinfo->mfiles [fd].filepos + offset ;
		break;
		
	case 2:					/* seek from tail */
		curoff = fsinfo->mfiles [fd].filesz + offset ;
		break;
	}
	
    if (curoff < 0 || curoff > fsinfo->mfiles [fd].filesz) 
        return (-SMERR_PERM);

	/* ONLY write mode need read to page buffer */
	if (!(fsinfo->mfiles [fd].mode & SMO_WRITE)) {
		fsinfo->mfiles [fd].filepos = curoff;		/* set current filepos */
		return (-SMERR_OK);
	}

	/* get current page and off */
	get_page_param (fd,&oldpage,&oldoff);
    
    /* set current filepos */
    fsinfo->mfiles [fd].filepos = curoff;
	
	/* get seek page and off  */
	get_page_param (fd,&curpage,&curoff);

	if (fsinfo->mfiles [fd].filepos < fsinfo->mfiles [fd].syncpos)      /* to move back  */
		movback = 1;
    else										/* move to end */
		movback = 0;
	
	/* switch page buff if page buff is changed */
	switch_pagebuff (fd,curpage);
	
	/* move back ,renew syncpos if changed */
	if (movback && fsinfo->mfiles [fd].syncpos < fsinfo->mfiles [fd].filesz)
		fsinfo->mfiles [fd].syncpos = fsinfo->mfiles [fd].filesz;

	/* if page changed and position moved back, mark it as dirt page */
	if (curpage != oldpage) 	
		fsinfo->mfiles [fd].isnewpage  = (movback != 1);
	
	return (-SMERR_OK);
}

static int _smfs_filestat (int fd,filestat *stat)
{
	check_fs_init ();
	
	/* fd check */
	if (fd < 0 || fd >= NR_FILE || !fsinfo->mfiles [fd].valid) 
		return  (-SMERR_NOSRC);
	
	stat [0] = fsinfo->mfiles [fd];
	
	return (-SMERR_OK);
}

int smfs_filestat (const char *filename,filestat *stat)
{
	int fd = -1;
	
	fd = smfs_open (filename,SMO_READ,0);
	
	if (fd >= 0) {
		
		_smfs_filestat (fd,stat);
		
		smfs_close (fd,0);
		
		return (0);
	}
	return (-SMERR_NOSRC);
}

int smfs_fsstat (fsstat *fs)
{
	fs [0] = fsinfo [0];
	
	return (-SMERR_OK);
}

int smfs_ftell (int fd)
{
	check_fs_init ();
	
	/* fd check */
	if (fd < 0 || fd >= NR_FILE || !fsinfo->mfiles [fd].valid) 
		return  (-SMERR_NOSRC);
	
	return (fsinfo->mfiles [fd].filepos);
}

int smfs_fsize (const char *filename) 
{
	int fsize  = 0;
	
	check_fs_init ();
	
	if (filename) {
		int fd = smfs_open (filename,SMO_READ,0);
		
		if (fd >= 0) {
			fsize = fsinfo->mfiles [fd].filesz;
			smfs_close (fd,0);
		}
	}
	
	return (fsize);
}

int smfs_ffree (void)
{
	check_fs_init ();
	
	return ((get_free_pages ()) * fsinfo->sblk.page_size);
}

int smfs_frename (const char *oldname,const char *newname) 
{
    int					fd  = -1;
    int					ret = -1;
    struct _devfile     *filehdr = NULL;
	unsigned char		*pagebuf = NULL;
    
    if (!oldname || !newname) 
        return (-SMERR_ARG);
    
    if (strlen (newname) > FNAMELEN)
        return (-SMERR_ARG);
    
    check_fs_init () ;

	/* check if this file is in writing process 
	 */
	for ( ; fd < NR_FILE ; fd ++) {
		if (fsinfo->mfiles [fd].valid && !strcmp ((const char *) fsinfo->mfiles [fd].fname,oldname)) {
			XDEBUG (0,"File is busy.");
			return (-SMERR_PERM);		/* file is busy */
		}
	}

    /* to check if newname file exist 
	 */
    if ((fd = smfs_open (newname,SMO_READ,0))) {
		/* if newname file exist ,quit ...
		 */
        smfs_close (fd,0);
        return (-SMERR_PERM);
    }
    
    /* if newname file does not exist 
	 * check if oldname exist 
	 */
    if ((fd = smfs_open (oldname,SMO_READ,0)) >= 0) {	/* Just need read mode */
		/* OK,oldname file does exist ,then modify filehdr
		 */

		/* alloc page buffer for this page */
		if (!(pagebuf = (unsigned char *) MALLOC (fsinfo->sblk.page_size))) {
			XDEBUG (0,"No more memory");
			smfs_close (fd,0);
			return (-SMERR_OOM);
		}

		/* read whole page to page buffer */
        ll_read_bytes (fsinfo->mfiles [fd].filehdr_page_nr * fsinfo->sblk.page_size,pagebuf,fsinfo->sblk.page_size);
        
		/* modify filehdr */
		filehdr = (struct _devfile *) (pagebuf + FILE_HDR_PAGEHDRSIZE) ;
		strcpy (filehdr->fname,newname);
		filehdr->hashkey = HASHKEY (newname);

		/* set a opt log */
		alloclog4opt (fd,LOG_RENAME,(unsigned short) fsinfo->mfiles [fd].filehdr_page_nr,0xffff);

		/* erase old page */
		ll_ioctl (SMIOCTL_ERASE_PAGE,(unsigned char *) (fsinfo->mfiles [fd].filehdr_page_nr),0);

		/* write back to page buffer */
		ll_write_bytes (fsinfo->mfiles [fd].filehdr_page_nr * fsinfo->sblk.page_size,pagebuf,fsinfo->sblk.page_size);

		/* close file stream */
        smfs_close (fd,0);

		/* free log */
		free_log (&fsinfo->mfiles [fd].open_log);

		/* free page buffer */
		FREE (pagebuf);
		pagebuf = NULL;

        return (-SMERR_OK);
    }
    
    return (-SMERR_PERM);
}

/* close file */
int smfs_close (int fd,void *arglist)
{
	int curpage = 0,curoff = 0;
	unsigned		magic = USED_FLAG;

	check_fs_init ();
	
	/* fd check */
	if (fd < 0 || fd >= NR_FILE || !fsinfo->mfiles [fd].valid) 
		return  (-SMERR_NOSRC);
	
	if (!fsinfo->mfiles [fd].cnt --) {
		XDEBUG (1,"Close non-opened file");
		fsinfo->mfiles [fd].cnt = 0;
		return (-SMERR_ARG);
	}
	
	XDEBUG (0,"Close file,File pos = %d ",fsinfo->mfiles [fd].filepos);
	
	if (fsinfo->mfiles [fd].mode & (SMO_WRITE)) {
        XDEBUG (0,"Write File hdr");
        
        if (fsinfo->mfiles [fd].filepos > fsinfo->mfiles [fd].filesz) {
            fsinfo->mfiles [fd].filesz = fsinfo->mfiles [fd].filepos;
		}

		/* if we make filehdr failed ,then regroup the filehdr and rebuild the file hdr page 
		 * trick :
		 * use the fake write (with a same used_flag written in the filehdr page 
		 * to make the filehdr dirt to sync to device 
		 */
		if (put_file_hdr (fsinfo->mfiles [fd].filehdr_page_nr,(struct _devfile *) &fsinfo->mfiles [fd]) < 0) {
			switch_pagebuff (fd,fsinfo->mfiles [fd].filehdr_page_nr);
			fsinfo->mfiles [fd].isnewpage = 0;

			/* fake write */
			write_page_bytes (fsinfo->mfiles [fd].filehdr_page_nr,0,
							(const unsigned char *) &magic,sizeof (magic));	/* make hdr dirt */
		}

        XDEBUG (0, "Close file %s",fsinfo->mfiles [fd].fname );
		
		/* write file hdr page */
        free_pagebuff (fd);
		
		/* operation is done */
		free_log (&fsinfo->mfiles [fd].open_log);
		free_log (&fsinfo->mfiles [fd].opt_log);
	}

	/* clear fd table */
	memset ((void *) &fsinfo->mfiles [fd],0,sizeof (fsinfo->mfiles [fd]));

	fsinfo->mfiles [fd].open_log.log_idx = -1;
	fsinfo->mfiles [fd].open_log.cmd	= LOG_NULL;
	
	fsinfo->mfiles [fd].opt_log.log_idx	= -1;
	fsinfo->mfiles [fd].opt_log.cmd		= LOG_NULL;
	return (-SMERR_OK);
}

int  find_first (struct _findfileinfo *pfi)
{
    int fd = 0;
    
    if (!pfi)
        return (-SMERR_ARG);
    
	for (; fd < NR_FILE ; fd ++) {
		if (fsinfo->mfiles [fd].mode & SMO_WRITE) {
			/* there is some file writing the device which 
			 * is not allowed when we are listing files 
			 */
            XDEBUG (0,"Some file is writing ..");
			return (-SMERR_PERM);		/* file is busy */
		}
	}
    
    memset ((void *) pfi,0,sizeof (*pfi));
    pfi->ready = 1;                           /* ready */
	pfi->fileindex = 0;
	pfi->filecnt = 0;
    return (-SMERR_OK);
}

int  find_next (struct _findfileinfo *pfi)
{
    int  pagenr = 0;
    
	check_fs_init ();
    
    if (!pfi && !pfi->ready)
        return (-SMERR_ARG);

	for ( ; pfi->fileindex < fsinfo->sblk.dev_files_nr ; pfi->fileindex ++) {
		if (!(pagenr = get_filehdr_pagenr (pfi->fileindex)))
			continue;
		get_file_hdr (pagenr,&pfi->filehdr);
		pfi->fileindex ++;
		pfi->filecnt ++;
		return (-SMERR_OK);
	}
    
    /* no more file */
    return (-SMERR_NOSRC);
}

int list_all_files (void) 
{
    struct _findfileinfo fi;
    
    if (!find_first (&fi)) {
        while (!find_next (&fi)) {
            printf( "Name: %20s,Make time: %08X,File size: %dB\n",
				fi.filehdr.fname,fi.filehdr.ctime,fi.filehdr.filesz );
        }
        printf ("Total File cnt = %d \r\n",fi.filecnt);
        printf ("Free Space %dBytes\r\n",smfs_ffree ());
    }
    return (-SMERR_OK);
}

