#include <WINDOWS.H>
#include <stdio.h>
#include "src\\smfs.h"
#include <conio.H>

/* simple fs demo 
 */
char ch [15000];

#define MAX_FILES		(4000)
#define WR_FILE			1
#define RD_FILE			1
#define RM_FILE			1
#define MULTI_WR		1
#define MULTI_RD		1
#define TRUNC_FILE		1
#define WR_RM			1

int main (void) 
{
	int		fd = 0;
	int		fd1 = 0,fd2 = 0;
	int		m = 0;
	char	filen [64] = {0};

	/* attach */
	while (smfs_init ()) {
		smfs_uninit ();
		smfs_mkfs (0);
		//system ("pause");
	}

	printf ( "size = %d \n",sizeof (*fsinfo));

	smfs_uninit ();

	return (0);

#if	TRUNC_FILE
	if ((fd1 = smfs_open ("helloA.txt",SMO_WRITE | SMO_CREATE | SMO_TRUNC,0)) >= 0) {
		memset (ch,'B',sizeof (ch));
		smfs_write (fd1,ch,sizeof (ch),0);
		smfs_close (fd1,0);
	}
#endif

#if	MULTI_WR
	if ((fd1 = smfs_open ("helloA.txt",SMO_WRITE | SMO_CREATE,0)) >= 0) {
		if ((fd2 = smfs_open ("helloB.txt",SMO_WRITE | SMO_CREATE,0)) >= 0) {
			sprintf (filen,"%s","Hello world");

			memset (ch,'B',sizeof (ch));
			smfs_write (fd2,ch,sizeof (ch),0);
			smfs_write (fd1,ch,sizeof (ch),0);

			smfs_close (fd2,0);
		}
		smfs_close (fd1,0);
	}
#endif

#if MULTI_RD
	if ((fd1 = smfs_open ("helloA.txt",SMO_READ,0)) >= 0) {
		if ((fd2 = smfs_open ("helloB.txt",SMO_READ,0)) >= 0) {

			memset (ch,'B',sizeof (ch));

			smfs_read (fd2,ch,sizeof (ch),0);

			printf ( "source = %s \n",ch);

			smfs_read (fd1,ch,sizeof (ch),0);

			printf ( "source = %s \n",ch);
			
			smfs_close (fd2,0);
		}
		smfs_close (fd1,0);
	}	
#endif


#if WR_FILE
	/* write file and write file */
 	for (m = 0 ; m < MAX_FILES ; m ++) {
		sprintf (filen,"file%d.txt",m);
		printf ("new filename = %s \n",filen);

		if ((fd = smfs_open (filen,SMO_WRITE | SMO_CREATE,0)) >= 0) {

			memset (ch,'B',sizeof (ch));
			smfs_write (fd,(const void *) &ch,sizeof (ch),0);

			smfs_close (fd,0);
		} 
	}
#endif

#if RD_FILE
	/* read file */
	for (m = 0 ; m < MAX_FILES ; m ++) {
		sprintf (filen,"file%d.txt",m);
		printf ("read filename = %s \n",filen);

		if ((fd = smfs_open (filen,SMO_READ,0)) >= 0) {
			char ch ;
			int i = 0;

			while (0 != smfs_read (fd,(void *) &ch,sizeof (ch),0)) {
				if (ch != 'B')
					printf ("Hello world i = %d , %d \n",i,ch);
				i ++;
			}
			printf ("All matched!\n");
			smfs_close (fd,0);
		}
	}
#endif

#if RM_FILE
	/* remove file */
	for (m = 0 ; m < MAX_FILES ; m ++) {
		sprintf (filen,"file%d.txt",m);
		printf ("remove filename = %s \n",filen);
		smfs_unlink (filen,0);
	}
#endif

// 	fd1 = smfs_open ("AAAAA.TXT",SMO_CREATE | SMO_WRITE,0);
// 	fd1 = smfs_open ("BBBBB.TXT",SMO_CREATE | SMO_WRITE,0);
// 	fd1 = smfs_open ("CCCCC.TXT",SMO_CREATE | SMO_WRITE,0);

#if WR_RM
	/* write file and write file */
	for (m = 0 ; m < MAX_FILES ; m ++) {
		sprintf (filen,"dfile%d.txt",m);
		printf ("new filename = %s \n",filen);
		
		if ((fd = smfs_open (filen,SMO_WRITE | SMO_CREATE,0)) >= 0) {
			memset (ch,'B',sizeof (ch));
			smfs_write (fd,(const void *) &ch,sizeof (ch),0);
			smfs_close (fd,0);
		} 
		smfs_unlink (filen,0);
	}
#endif

	list_all_files ();

	/* detach */
	smfs_uninit ();
	return (0);

}