#ifndef __PRIVATE_H__
#define __PRIVATE_H__

extern 				struct _fsinfo			*fsinfo;

struct _devlog {
	unsigned char	cmd;					/* in what operation cmd  ? */
	char			opt_pending:1;			/* is this operation pending yet ? */
	char			:0;						/* padding */
	unsigned short	param1;					/* cmd parameter 1 */
	unsigned short	param2;					/* cmd parameter 2 */
};

struct _memlog {
	unsigned char	cmd;					/* in what operation cmd  ? */
	char			opt_pending:1;			/* is this operation pending yet ? */
	char			:0;						/* padding */
	unsigned short	param1;					/* cmd parameter 1 */
	unsigned short	param2;					/* cmd parameter 2 */

	/* in memory */
	int				log_idx;				/* log index */
};

#define		LOG_NULL			(255)
#define		LOG_NEW_FILE		(0)			/* new file */
#define		LOG_WR_FILE			(1)			/* write file */
#define		LOG_RM_FILE			(2)			/* remove file */
#define		LOG_MV_FILE			(3)			/* move file */
#define		LOG_TRUNC_FILE		(4)			/* truncate file */
#define		LOG_OVERWRITE		(5)			/* overwrite file */
#define		LOG_APPEND			(6)			/* append file */
#define		LOG_REWRITE			(7)			/* rewrite page */
#define		LOG_RENAME			(8)			/* rename file header */
#define		LOG_DELETE			(9)			/* unlink a file */

#ifdef __cplusplus
extern "C" {
#endif
	
	extern int			alloc_page (void);
	extern void			free_page (int ipage);
	extern int			load_sysinfo (void) ;
	
	extern void			init_log (void) ;
	extern int			alloc_log (struct _memlog *memlog,unsigned char cmd,unsigned short param1,unsigned short param2);
	extern int			alloclog4opt  (int fd,unsigned char cmd,unsigned short param1,unsigned short param2);
	extern int			alloclog4open (int fd,unsigned char cmd,unsigned  short param1,unsigned short param2);
	extern int			free_log (struct _memlog *p_log) ;
	
	extern int			find_filehdr_pagenr (const char *filename,int *index) ;
	extern int			find_free_fileindex (void);
	extern int			add_filehdr_pagenr (int index,int pagenr);
	extern int			remove_filehdr_pagenr (int index);
	extern int			get_filehdr_pagenr (int index);
	extern int			safe_trunc_file_body (int filehdr_next_page,int buffered);
	
	extern int			get_next (int entry);
	extern int			put_next (int pagenr,int next);
	extern int			check_fs (void) ;
	
	extern void			PANIC	(const char *fmt,...) ;
	extern void			XDEBUG	(int level,const char *fmt,...) ;
	extern int			HASHKEY (const char *string) ;
	
#ifdef __cplusplus
}
#endif

#endif /* end __PRIVATE_H__ */