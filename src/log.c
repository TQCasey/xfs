#include "smfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "private.h"

/* log system to record what smfs have done to the flash 
 *										
 *										2015/9/12 casey
 */

#define	BACKUP_LOG_MAGIC	(0xDEAD)
#define COUNT_OF(x)			(sizeof (x) / sizeof (x[0]))

struct _backup_log {
	unsigned	short		magic ;
	unsigned	short		nr_logs;
};

/* return the offset of free backup log */
static int find_free_bkup_log (int nr_logs) 
{
	struct _backup_log		backup_log	= {0};
	unsigned				offset		= fsinfo->sblk.log_backup_page * fsinfo->sblk.page_size;
	unsigned				end			= offset + fsinfo->sblk.page_size;

	while (offset < end) {
		/* read backup log */
		ll_read_bytes (offset,(unsigned char *) &backup_log,sizeof (backup_log));
		if (backup_log.magic != BACKUP_LOG_MAGIC || backup_log.nr_logs < 0) 
			break;
		offset	+= (sizeof (backup_log) + backup_log.nr_logs * sizeof (struct _devlog));
	}

	/* if can be put in */
	if ((offset + sizeof (backup_log) + nr_logs * sizeof (struct _devlog)) <= end) 
		return (offset);
	return (-1);
}

/* backup memlogs to backup log page */
static void backup_memlogs (int offset,int nr_logs) 
{
	int		fd = 0;
	struct	_backup_log	backup_log;

	backup_log.magic	= BACKUP_LOG_MAGIC;
	backup_log.nr_logs	= nr_logs;

	/* write this backup log header */
	ll_write_bytes (offset,(const unsigned char *) &backup_log,sizeof (backup_log));

	offset += sizeof (backup_log);

	/* write this backup log body */
	for (fd = 0 ; fd < NR_FILE ; fd ++) {
		
		/* open log */
		if (fsinfo->mfiles [fd].open_log.cmd != LOG_NULL 
			&& fsinfo->mfiles [fd].open_log.log_idx >= 0 
			&& fsinfo->mfiles [fd].open_log.opt_pending) 
		{
			ll_write_bytes (offset + fsinfo->mfiles [fd].open_log.log_idx * sizeof (struct _devlog),
							(const unsigned char *) &fsinfo->mfiles [fd].open_log,
							sizeof (struct _devlog));
		}
		
		/* opt log */
		if (fsinfo->mfiles [fd].opt_log.cmd != LOG_NULL 
			&& fsinfo->mfiles [fd].opt_log.log_idx >= 0
			&& fsinfo->mfiles [fd].opt_log.opt_pending) 
		{
			ll_write_bytes (offset + fsinfo->mfiles [fd].opt_log.log_idx * sizeof (struct _devlog),
							(const unsigned char *) &fsinfo->mfiles [fd].opt_log,
							sizeof (struct _devlog));
		}
	}
	/* backup done */
}

/* restore logs to log pages log */
static void restore_logs (void)
{
	int		offset			= fsinfo->sblk.log_page * fsinfo->sblk.page_size;
	int		fd				= 0;

	/* write this backup log body */
	for (fd = 0 ; fd < NR_FILE ; fd ++) {
		
		/* open log */
		if (fsinfo->mfiles [fd].open_log.cmd != LOG_NULL 
			&& fsinfo->mfiles [fd].open_log.log_idx >= 0 
			&& fsinfo->mfiles [fd].open_log.opt_pending) 
		{
			ll_write_bytes (offset + fsinfo->mfiles [fd].open_log.log_idx * sizeof (struct _devlog),
				(const unsigned char *) &fsinfo->mfiles [fd].open_log,
				sizeof (struct _devlog));
		}
		
		/* opt log */
		if (fsinfo->mfiles [fd].opt_log.cmd != LOG_NULL 
			&& fsinfo->mfiles [fd].opt_log.log_idx >= 0
			&& fsinfo->mfiles [fd].opt_log.opt_pending) 
		{
			ll_write_bytes (offset + fsinfo->mfiles [fd].opt_log.log_idx * sizeof (struct _devlog),
				(const unsigned char *) &fsinfo->mfiles [fd].opt_log,
				sizeof (struct _devlog));
		}
	}
}

static void clear_backup_log_pending (int offset)
{
	struct	_backup_log		backup_log;
	int						nr_pages = 0,i = 0;
	struct _devlog			devlog;

	ll_read_bytes (offset,(unsigned char *) &backup_log,sizeof (backup_log));

	if (backup_log.magic != BACKUP_LOG_MAGIC) 
		return ;

	if ((nr_pages = backup_log.nr_logs) <= 0) 
		return ;

	offset += sizeof (backup_log);

	for (i = 0 ; i < nr_pages ; i ++) {
		ll_read_bytes (offset + i * sizeof (devlog),(unsigned char *) &devlog,sizeof (devlog));
		if (devlog.cmd != LOG_NULL && devlog.opt_pending) {
			devlog.opt_pending = 0;
			ll_write_bytes (offset + i * sizeof (devlog),(unsigned char *) &devlog,sizeof (devlog));
		}
	}
}

static void swap (short *a, short *b)
{
	short    temp;

	temp	= *a;
	*a		= *b;
	*b		= temp;
}

static void bubble_sort (short a [], int n)
{
	int j, k;
	int flag;
	
	flag = n;
	while (flag > 0) {
		k = flag;
		flag = 0;
		for (j = 1; j < k; j++) {
			if (a[j - 1] > a[j]) {
				swap (&a[j - 1], &a[j]);
				flag = j;
			}
		}
	}
}

static int find_index (short a [],int n,int value)
{ 
	int i = 0 ;

	for (i = 0 ; i < n ; i ++) {
		if (a [i] == value)
			return (i);
	}
	return (-1);
}

static int re_order_logs (void) 
{
	int		fd = 0;
	int		cnt = 0;
	short	lognr [NR_FILE << 1] = {0};	/* each file have 2 log slots */

	for (fd = 0 ; fd < NR_FILE ; fd ++) {

		/* open log */
		if (fsinfo->mfiles [fd].open_log.cmd != LOG_NULL 
			&& fsinfo->mfiles [fd].open_log.log_idx >= 0 
			&& fsinfo->mfiles [fd].open_log.opt_pending) 
		{
			lognr [cnt] = fsinfo->mfiles [fd].open_log.log_idx;
			cnt ++;
		}

		/* opt log */
		if (fsinfo->mfiles [fd].opt_log.cmd != LOG_NULL 
			&& fsinfo->mfiles [fd].opt_log.log_idx >= 0
			&& fsinfo->mfiles [fd].opt_log.opt_pending) 
		{
			lognr [cnt] = fsinfo->mfiles [fd].opt_log.log_idx;
			cnt ++;
		}
	}

	/* bubble sort */
	bubble_sort (lognr,cnt);

	/* reset log index (range 0 ~ 7) */
	for (fd = 0 ; fd < NR_FILE ; fd ++) {

		/* open log */
		if (fsinfo->mfiles [fd].open_log.cmd != LOG_NULL 
			&& fsinfo->mfiles [fd].open_log.log_idx >= 0 
			&& fsinfo->mfiles [fd].open_log.opt_pending) 
		{
			fsinfo->mfiles [fd].open_log.log_idx = find_index (lognr,cnt,fsinfo->mfiles [fd].open_log.log_idx);
		}
		
		/* opt log */
		if (fsinfo->mfiles [fd].opt_log.cmd != LOG_NULL 
			&& fsinfo->mfiles [fd].opt_log.log_idx >= 0
			&& fsinfo->mfiles [fd].opt_log.opt_pending) 
		{
			fsinfo->mfiles [fd].opt_log.log_idx = find_index (lognr,cnt,fsinfo->mfiles [fd].opt_log.log_idx);
		}
	}

	return (cnt);
}

/* to init log system */
void init_log (void)
{
	/* first to check if there is some backup log out there */
	struct _backup_log		backup_log	= {0};
	unsigned				offset		= fsinfo->sblk.log_backup_page * fsinfo->sblk.page_size;
	unsigned				end			= offset + fsinfo->sblk.page_size;
	unsigned				prev_offset = offset;
	unsigned short			i			= 0;
	struct _devlog			devlog		= {0};
	unsigned short			pagenr		= 0;
	unsigned char			has_pending = 0;
	unsigned 				log_off		= 0;
	
	while (offset < end) {
		/* read backup log */
		ll_read_bytes (offset,(unsigned char *) &backup_log,sizeof (backup_log));
		if (backup_log.magic != BACKUP_LOG_MAGIC || backup_log.nr_logs < 0) 
			break;
		prev_offset = offset;
		offset	+= (sizeof (backup_log) + backup_log.nr_logs * sizeof (struct _devlog));
	}

	/* get the last validate backup log record */
	ll_read_bytes (prev_offset,(unsigned char *) &backup_log,sizeof (backup_log));

	if (backup_log.magic != BACKUP_LOG_MAGIC || !backup_log.nr_logs)
		return;

	/* to check if there is something pending there */
	/* skip backlog header */
	prev_offset += sizeof (backup_log);	

	/* save the offset */
	offset = prev_offset;

	/* for each device logs and check if has pending operations */
	for (i = 0 ; i < backup_log.nr_logs ; i ++) {
		ll_read_bytes (offset,(unsigned char *) &devlog,sizeof (devlog));
		if (devlog.opt_pending) {
			has_pending = 1;
			break;
		}
		offset += sizeof (devlog);
	}

	if (!has_pending)	/* no pending yet , quit */
		return ;

	/* yes if has pending logs */
	offset		= prev_offset;
	log_off		= fsinfo->sblk.log_page * fsinfo->sblk.page_size;

	/* erase all the log pages anyway */
	for (pagenr = fsinfo->sblk.log_page ; pagenr < fsinfo->sblk.start_page ; pagenr ++) 
		ll_ioctl (SMIOCTL_ERASE_PAGE,(unsigned char *) pagenr,0);

	/* copy old logs to the log pages */
	for (i = 0 ; i < backup_log.nr_logs ; i ++) {
		/* read devlog from backup log */
		ll_read_bytes (offset,(unsigned char *) &devlog,sizeof (devlog));

		/* copy to log page log */
		if (devlog.opt_pending) {
			/* write back to device log */
			ll_write_bytes (log_off,(const unsigned char *) &devlog,sizeof (devlog));

			/* clear pending state */
			devlog.opt_pending  = 0;
		}
		/* clear backup log pending state */
		ll_write_bytes (offset,(unsigned char *) &devlog,sizeof (devlog));
	}
}

/* generic log allocation function */
int alloc_log (struct _memlog *memlog,unsigned char cmd,unsigned short param1,unsigned short param2)
{
	int				maxn = (fsinfo->sblk.start_page - fsinfo->sblk.log_page) 
						  * fsinfo->sblk.page_size / sizeof (struct _devlog);
	int				offset = fsinfo->sblk.page_size * fsinfo->sblk.log_page;
	int				free_bkup_log_offset	= 0;
	int				nr_logs					= 0;
	int				i						= 0;

	if (!memlog)
		return (-1);

	fsinfo->log_idx ++ ;	/* next validate record */

	if (fsinfo->log_idx >= maxn) {
		/* get nr of logs and re order logs */
		nr_logs = re_order_logs ();

		/* log is full ,we need backup memory logs and erase all log pages */
		if (!nr_logs) {
			/* if no previous logs exist. no need to backup logs */
		} else {
			/* @1 find a free backup_log record */
			if ((free_bkup_log_offset = find_free_bkup_log (nr_logs)) < 0) {
				/* if not found ,it means backup log is full,we need erase it all */
				ll_ioctl (SMIOCTL_ERASE_PAGE,(unsigned char *) fsinfo->sblk.log_backup_page,0);

				/* reset free backup log record index */
				free_bkup_log_offset = fsinfo->sblk.log_backup_page * fsinfo->sblk.page_size;
			}

			/* @2 write memory logs into backup log records */
			backup_memlogs  (free_bkup_log_offset,nr_logs);
		}

		/* @3 erase all log pages */
		for (i = fsinfo->sblk.log_page  ; i < fsinfo->sblk.start_page ; i ++) {
			ll_ioctl (SMIOCTL_ERASE_PAGE,(unsigned char *) i,0);	/* erase all log page */
		}

		if (nr_logs) {
			/* @4.1 copy memlog from fsinfo to the log page logs */
			restore_logs ();

			/* @4.2 clear backup pending bits */
			clear_backup_log_pending (free_bkup_log_offset);
		} else {
			/* @4 nothing to do */
		}

		/* @5 reset log index */
		fsinfo->log_idx = 0;
	}
	XDEBUG (0,"Alloc a log Index : %d",fsinfo->log_idx);

	/* set operation */
	memlog->cmd			= cmd;
	memlog->opt_pending	= 1;
	memlog->param1		= param1;
	memlog->param2		= param2;
	memlog->log_idx		= fsinfo->log_idx; 

	/* write log right now */
	ll_write_bytes (offset + fsinfo->log_idx * sizeof (struct _devlog),
					(unsigned char *) memlog,sizeof (struct _devlog));
	
	/* return current log index */
	return (fsinfo->log_idx);
}

/* generic log free function */
int free_log (struct _memlog *p_log) 
{
	int				offset = fsinfo->sblk.page_size * fsinfo->sblk.log_page;

	if (!p_log || p_log [0].log_idx < 0 || p_log [0].cmd == LOG_NULL) 
		return (-SMERR_ARG);
	if (!p_log [0].opt_pending) {
		XDEBUG (1,"Free free log");
		return  (-SMERR_ARG);
	}
	/* log opt pending clean */
	p_log [0].opt_pending = 0;		

	XDEBUG (0,"Free log Index : %d",p_log->log_idx);

	/* sync data to device */
	ll_write_bytes (offset + p_log [0].log_idx * sizeof (struct _devlog),
				   (const unsigned char *) p_log,sizeof (struct _devlog));

	p_log [0].log_idx = -1;		/* reset log index */
	return (-SMERR_OK);
}

/* alloc a log for open operation */
int alloclog4open (int fd,unsigned char cmd,unsigned  short param1,unsigned short param2)
{
	/* fd check */
	if (fd < 0 || fd >= NR_FILE || !fsinfo->mfiles [fd].valid) 
		return  (-SMERR_ARG);

	/* check if previous open log is pending ... */
	if (fsinfo->mfiles [fd].open_log.log_idx >= 0 && fsinfo->mfiles [fd].open_log.opt_pending) {
		XDEBUG (1,"Previous open log is pending..");
		return (-SMERR_PERM);
	}

	/* alloc a new log for file open */
	if (alloc_log (&fsinfo->mfiles [fd].open_log,cmd,param1,param2) < 0) {
		XDEBUG (2,"Alloc log for open mode failed");
		return (-SMERR_NOSRC);
	}
	return (-SMERR_OK);
}

/* alloc a log for opt operation */
int alloclog4opt (int fd,unsigned char cmd,unsigned short param1,unsigned short param2)
{
	/* fd check */
	if (fd < 0 || fd >= NR_FILE || !fsinfo->mfiles [fd].valid) 
		return  (-SMERR_ARG);
	
	/* check if previous opt log is pending ... */
	if (fsinfo->mfiles [fd].opt_log.log_idx >= 0 && fsinfo->mfiles [fd].opt_log.opt_pending) {
		XDEBUG (1,"Previous opt log is pending..");
		return (-SMERR_PERM);
	}
	
	/* alloc a new log for file opt */
	if (alloc_log (&fsinfo->mfiles [fd].opt_log,cmd,param1,param2) < 0) {
		XDEBUG (2,"Alloc log for open mode failed");
		return (-SMERR_NOSRC);
	}
	return (-SMERR_OK);
}
