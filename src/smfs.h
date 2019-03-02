#ifndef __SMFS_H__
#define __SMFS_H__

#include "private.h"

/* this is the errorcode for smfs 
 */
#define SMERR_OK				(0)
#define SMERR_NOFD				(1)
#define SMERR_NOPAGE			(2)
#define SMERR_OOM				(3)
#define SMERR_HASH				(4)
#define SMERR_IO				(5)
#define SMERR_TMO				(6)
#define SMERR_ARG				(7)
#define SMERR_NOSRC				(8)
#define SMERR_PERM				(9)
#define SMERR_NOFS				(10)

/* for seek offset */
#if 0
#ifndef SEEK_SET
#define SEEK_SET				(0)
#endif

#ifndef SEEK_CUR
#define SEEK_CUR				(1)
#endif

#ifndef SEEK_END
#define SEEK_END				(2)
#endif
#endif

#define SMFS_MAGIC				(0x900DBAAA)

#ifdef __cplusplus
extern "C" {
#endif

#define	NR_FILE					(4)		/* max open file in memory */
#define NR_PAGEBUF				(4)		/* buffer page numbers */
#define FILESIZE_MOD_NR			(300)	/* file header modify times nr */
#define FILELINK_MOD_NR			(0)		/* link area modify times nr */
#define NR_DEV_FILE				(750)	/* max device files (must be multiple of 8) */
#define FNAMELEN				(32)	/* max file name length */
#define	USED_FLAG				(0xABBCCDDA) /* flag for used */
  
struct  _devfile {
	/* constant part */
	unsigned		hashkey;			/* device hashkey */
	char			fname [FNAMELEN+1];	/* file name */
	unsigned		ctime;				/* create time */
	int				next_page_nr;		/* next page number */

	/* can be modified but still in device part */

	int				filesz;				/* file size */
};

struct _pagebuf {
	char			valid;				/* validate flag */
	char			dirt;				/* dirt flag */
	short			pagenr;				/* page number */
	unsigned char	*pagebuf;			/* page buffer pointer */
	int				fd;					/* belongs to which file */
};

struct  _memfile {
	/* constant part */
	unsigned		hashkey;			/* device hashkey */
	char			fname [FNAMELEN+1];	/* file name */
	unsigned		ctime;				/* create time */
	int				next_page_nr;		/* next page number */

	/* can be modified but still in device part */

	int				filesz;				/* file size */

	/* belows are in memory */

	char			valid:1;			/* valid flag */
	char			dirt:1;				/* dirt flag */
	char			isnewpage:1;		/* is this a new page */
	char			:0;					/* padding .. */
	char			cnt;				/* count of reference */
	unsigned short	mode;				/* open mode */
	int				filehdr_page_nr;	/* file header page number */
	int				filepos;			/* logical current file position */
	int				syncpos;			/* device sync position */
	short			fileindex;			/* fileindex */

	struct _memlog	open_log;			/* log for open */
	struct _memlog  opt_log;			/* log for operation */
};

/* super block */
struct _sblk {
	unsigned		magic ;				/* file system magic */
	unsigned		page_size;			/* page size */
	unsigned		page_nr;			/* nr of pages */
	int				log_page;			/* log page */
	int				start_page;			/* start page number */
	int				dev_files_nr;		/* device files max count */
	int				log_backup_page;	/* log backup page */
};

/* configuration information */
struct	_fsinfo {
	struct _sblk	sblk;				/* super block of smfs */

	/* follows are in memory */
	char			inited;				/* is file system inited yet ?? */
	int				cur_bmp_pos;		/* current bitmap position */

	/* bmp pages */
	unsigned char	*bmap ;				/* bitmap buffer */
	unsigned short	*filetbl;			/* file table */
	int				nr_files;			/* files number */
	int				nr_pages;			/* pages number */

	/* current log index */
	int				log_idx;			/* valid log index */

	/* file descriptor tables  */
	struct _memfile	mfiles [NR_FILE];	/* file descriptor table */

	/* page buff can not be connected with mfiles */
	struct _pagebuf	pagebufs [NR_PAGEBUF];
};

typedef struct _memfile	filestat;
typedef struct _fsinfo	fsstat;

struct _findfileinfo {
    int                 fileindex;
    int                 filecnt;
    int                 ready;
    struct  _devfile    filehdr;    
};

struct _pagehdr {
	unsigned			magic ;							/* page hdr magic */
	int					next;							/* next page nr or next file entry */
};

/* bytes of link area */
#define FILE_LINK_AREA_SIZE		(FILELINK_MOD_NR << 1)

/* file header size area */
#define FILE_SIZE_AREA_SIZE		(FILESIZE_MOD_NR << 2)

/* hash size area */
#define FILE_HASH_AREA_SIZE		(HASH_TBL_LEN << 1)

/* file header page header size */
#define FILE_HDR_PAGEHDRSIZE	(sizeof (struct _pagehdr) + FILE_LINK_AREA_SIZE)

/* file body page header size */
#define FILE_BODY_PAGEHDRSIZE	(sizeof (struct _pagehdr))

/* file header size */
#define FILE_HDR_SIZE			(sizeof (struct _devfile))

/* file body offset */
#define FILE_BODY_OFFSET		(FILE_HDR_PAGEHDRSIZE + FILE_HDR_SIZE + FILE_SIZE_AREA_SIZE)

/* operation flags set */
#define SMO_NULL				(0x00)
#define SMO_READ				(0x01)
#define SMO_WRITE				(0x02)
#define SMO_CREATE				(0x04)
#define SMO_APPEND				(0x08)
#define SMO_TRUNC				(0x10)
#define SMO_OVERWRITE			(0x20)
#define SMO_FILE_EXISTS         (0x40)
  
/* ioctl flags set */
#define	SMIOCTL_ERASE_PAGE		(0x00)
#define	SMIOCTL_ERASE_FS		(0x01)
#define	SMIOCTL_GETTIME			(0x02)

/* low level api */
extern int	hwl_init			(struct _sblk *psblk);
extern int	hwl_uninit			(struct _fsinfo *pfsinfo) ;

extern void ll_erase_fs			(void);
extern int	ll_read_bytes		(int offset,unsigned char * buffer,int size);
extern int	ll_write_bytes		(int offset,const unsigned char * buffer,int size);
extern int	ll_ioctl			(int cmd,unsigned char *buffer,int buffersz);
extern int	read_page_bytes		(int pagenr,int offset,unsigned char *buffer,int size);
extern int	write_page_bytes	(int pagenr,int offset,const unsigned char *buffer,int size);
extern void*MALLOC              (int size);
extern void*FREE                (void *mem);

/* fs APIs */
extern int	smfs_init			(void);
extern int	smfs_uninit			(void);
extern int	smfs_mkfs			(void *arglist) ;
extern int	smfs_open			(const char *filename,unsigned char mode,void *arglist);
extern int	smfs_read			(int fd,void *buffer,int size,void *arglist);
extern int	smfs_write			(int fd,const void *buffer,int size,void *arglist);
extern int	smfs_close			(int fd,void *arglist);
extern int	smfs_seek			(int fd,int offset,int fromwhere,void *arglist);
extern int	smfs_unlink			(const char *filename,void *arglist);
extern int	smfs_fsstat			(fsstat *fs);
extern int	smfs_ftell			(int fd);
extern int	smfs_fsize			(const char *filename) ;
extern int	smfs_filestat		(const char *filename,filestat *stat);
extern int	smfs_ffree			(void);
extern int  smfs_frename        (const char *oldname,const char *newname) ;

/* */
extern int	list_all_files		(void) ;
extern int  find_first			(struct _findfileinfo *pfi);
extern int  find_next			(struct _findfileinfo *pfi);

/* stdio PROTOTYPES */
extern int  finit               (const char *driver) ;
extern int  funinit             (const char *driver);
extern int  fformat             (const char *drvier);
extern int  fdelete             (const char *filename);

#ifdef	__cplusplus
}
#endif

#endif
