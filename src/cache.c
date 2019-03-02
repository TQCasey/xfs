#include "smfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "private.h"

/* this is the most ugly part 
 * we need find a f(x) to hash (filename) ==> cache_index 
 * but we can't find it ,so code is like this below ,temporary
 *	
 *											casey 
 *											2015/9/21
 */


/* find file by name from hash table
 * if ok,return the filehdr page number 
 * if error,return error code 
 */
int find_filehdr_pagenr (const char *filename,int *index) 
{	
	int i = 0 ,pagenr = 0; 
	struct	_devfile	filehdr;

	/* we first compare their hashkey 
	 * if matched ,compare their filename then 
	 * if not matched ,continue to next block *
	 */
	struct	_devfile	*p_filehdr		= NULL/*&filehdr*/;
	int					hashkey			= HASHKEY (filename);
	int					off_hashkey		= ((int) (&p_filehdr->hashkey) - (int) p_filehdr);	/* hashkey offset */
	int					devhashkey		= 0;
	
	for (i = 0 ; i < fsinfo->sblk.dev_files_nr ; i ++) {
		if (0 != (pagenr = fsinfo->filetbl [i])) {
			read_page_bytes (pagenr,FILE_HDR_PAGEHDRSIZE + off_hashkey,
							(unsigned char *) &devhashkey,sizeof (devhashkey));
			if (devhashkey == hashkey) {	/* hashkey matched */
				read_page_bytes (pagenr,FILE_HDR_PAGEHDRSIZE,(unsigned char *) &filehdr,sizeof (filehdr));
				if (0 == strcmp (filehdr.fname,filename)) {
					*index = i;
					return (pagenr);
				}
			}
		}
	}
	*index = -1;
	return (0);
}

int find_free_fileindex (void)
{
	int i = 0;

	/* get a new file index */
	for (i = 0 ; i < fsinfo->sblk.dev_files_nr ; i ++) {
		if (!fsinfo->filetbl [i]) 
			return (i);
	}
	return (-1);
}

int	add_filehdr_pagenr (int index,int pagenr)
{
	fsinfo->filetbl [index]				= pagenr;
	return (0);
}

int remove_filehdr_pagenr (int index)
{
	/* wipe out in file table */
	fsinfo->filetbl [index] = 0;
	return (0);
}

int get_filehdr_pagenr (int index)
{
	return (fsinfo->filetbl [index]);
}

/* read page-in bytes 
 * if page is buffered ,read from page buffer,
 * if not ,read bytes from device directly .
 */
int read_page_bytes (int pagenr,int offset,unsigned char *buffer,int size)
{
	int i = 0 ;
	
	/* check bounds */
	if (size <= 0 || offset + size > (int) fsinfo->sblk.page_size)
		return (0);
	
	/* first read in page buffers */
	for (i = 0 ; i < NR_PAGEBUF ; i ++) {
		/* check if pagenr is buffered */
		
		if (fsinfo->pagebufs [i].valid) {
			/* if page is buffered */
			if (fsinfo->pagebufs [i].pagenr == pagenr && fsinfo->pagebufs [i].pagebuf) {
				memcpy ((void *) buffer,(const void *) (fsinfo->pagebufs [i].pagebuf + offset),size);
				return (size);
			}
		}
	}
	
	/* if no more page buffer ,read from device directly */
	return (ll_read_bytes (pagenr * fsinfo->sblk.page_size + offset,buffer,size));
}

/* get page buff ptr base on page nr if there is 
 */
void *get_page_buff (int pagenr,int *index)
{
	int i = 0;
	/* first write to page buffers */
	for (i = 0 ; i < NR_PAGEBUF ; i ++) {
		/* check if pagenr is buffered */
		
		if (fsinfo->pagebufs [i].valid) {
			/* if page is buffered */
			if (fsinfo->pagebufs [i].pagenr == pagenr && fsinfo->pagebufs [i].pagebuf) {
				*index = i;
				return (fsinfo->pagebufs [i].pagebuf);
			}
		}
	}
	*index = -1;
	return (NULL);
}

/* write page-in bytes 
 * if page is buffered ,write to page buffer ,
 * if not ,write to device directly ...
 */
int write_page_bytes (int pagenr,int offset ,const unsigned char *buffer,int size)
{
	int					i		= 0;
	unsigned char 	*pagebuf	= NULL;
	
	/* check bounds */
	if (size <= 0 || offset + size > (int) fsinfo->sblk.page_size)
		return (0);

	if ((pagebuf = (unsigned char *) get_page_buff (pagenr,&i))) {
		memcpy ((void *) (pagebuf + offset),(const void *) buffer,size);
		fsinfo->pagebufs [i].dirt = 1;
		return (size);
	}
	
	/* write buffer to device directly */
	return (ll_write_bytes (pagenr * fsinfo->sblk.page_size + offset,buffer,size));
}
