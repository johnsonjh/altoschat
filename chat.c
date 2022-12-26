/*
	This is the original "Altos" chatline system.
	The following comes from the original author (korn@altger) of this file:

--
	The user-interface for the chat-program.
	Actually, two processes:
	- one reading from the user's console, inter-
	  preting a few commands ( to be executed locally )
	  sending commands & text ( via the server-pipe )
	  to the server-process
	- one reading from the server-process ( thru an
	  additional pipe ) and displaying the text
	  at the user's console.
	Last modification: May 1987, Hans Korneder
--

	Small patching by Paolo Ventafridda (venta@i2ack.sublink.org)
	May 8, 1995 for Linux and Internet. I have also added a dirty who_am_i()
	in order to get the rlogin hostname for the list. It is ugly, i know.
*/

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <utmp.h>
#include <time.h>
#include <getopt.h>
#include <sys/param.h>

#include <pwd.h> 
#include <signal.h>
#include <unistd.h>
#include <utmp.h>
#include "chat.h"



/* global data */
struct chat_packet cp;    /* the data packet being sent to the server */
char   buf[128];          /* local data buffer                        */
char   logname[16];       /* the user's name ( read from /etc/passwd )*/
char   nickname[40];      /* the user's nickname ( from LOGNAME )     */
char   from[24];          /* the user's origin ( from FROM )          */
int    pipet=0;           /* pipe-file-descriptor to the server       */
char  *pfrom=0;           /* name of the pipe from the server         */
int    child=0;           /* child's pid                              */
int    parent;            /* parent's pid                             */
struct utmp uline;

static char *frm;

/* externals */
extern struct passwd *getpwuid();
/* extern int endpwent(); */
extern char *getenv();
extern int strlen();
extern char *mktemp();
extern int errno;

catch(s)
int s;
	{
	signal(s,(void *)catch);
	}

msg(text)
char *text;
	{
	write(1,text,strlen(text));
	}

filter(txt)
char *txt;
	{
	register i,k,l;
	l=strlen(txt);
	for(i=k=0; i<l; i++)
		{
		txt[i] &= 0x7f;
		if ( ( txt[i] < ' ' ) || ( txt[i]>126 ) ) continue;
		txt[k++] = txt[i];
		}
	txt[k] = 0;
	}

help()
	{
	msg(".x           exit this program\n");
	msg(".s           get the list of users on the chat\n");
	msg(".n name      change your nickname\n");
	msg(".h           get this page of text\n");
	msg(".w           get a list of users on the system\n");
	msg(".b           bell all users\n");
	msg(".m           send mail to the sysop\n");
	msg(".c #         switch to channel no <#>\n");
	msg(".p passwd    set a passwd for a channel\n");
	msg("!# text      send a private message to user <#>\n");
	}

log_off()
	{
	if ( pfrom )
		{
		char *pf=pfrom;
		pfrom=0;
		unlink(pf);
		}
	if ( child )
		{ 
		int cld=child; 
		child=0; 
		kill(cld,SIGKILL); 
		wait(&cld); 
		}
	if ( pipet )
		{
		int pt=pipet;
		pipet=0;
		sprintf(cp.cp_magic,"%5d,",UMAGICL);
		sprintf(cp.cp_pid  ,"%5d,",parent);
		write(pt,&cp,sizeof(cp));
		}
	exit(0);
	}

log_on()
	{
	sprintf(cp.cp_magic,"%5d,",UMAGIC1);
	sprintf(cp.cp_pid  ,"%5d,",parent);
	sprintf(cp.cp_text ,"%-8.8s%-32.32s%-40.40s%-16.16s",
		logname,nickname,pfrom,from);
	write(pipet,&cp,sizeof(cp));
	}

bad(text)
char *text;
	{
	write(2,text,strlen(text));
	log_off(); /* never should return */
	}

int get_line(fd)
int fd;
	{
	/*
	read some text;
	clean it; ( remove high bits, remove control chars )
	transfer it into cp_text;

	known 'problems':
	- i should not read character by character
	  (but i would have to manage the buffers for the .so-cmd)
	  (or use the stdio-functions)
	- on an EOF i should transfer the rest of a pending line
	*/

	int to;
	char c;
	/* i should use filter() instead.... */
	for(to=0; to<TEXTLEN; )
		{
		if ( read(fd,&c,1)<1 ) return 0;
		c &= 0x7f;
		if ( c==10 ) break;
		if ( c<' ' || c>126 ) continue;
		cp.cp_text[to++] = c;
		}
	if ( to<TEXTLEN ) cp.cp_text[to]=0;
	return 1;
	}

process(fd)
int fd;
	{
	while ( get_line(fd) )
		{
		/* check for local commands */
		if ( !strncmp(cp.cp_text,".w",2) )
			{ /* who is on the system */
			signal(SIGINT,SIG_DFL);
			system("/usr/bin/who");
			signal(SIGINT,(void *)log_off);
			msg("--- back in chat ---\n");
			continue;
			}
		if ( !strncmp(cp.cp_text,".m",2) )
			{ /* send mail to the sysop */
			msg("This is disabled by default- use SMTP!\n");
			continue;
			msg("Enter the mail-text; ");
			msg("End with a line with just a dot '.'\n");
			signal(SIGINT,SIG_DFL);
			system("/bin/mail root");
			signal(SIGINT,(void *)log_off);
			msg("--- back in chat ---\n");
			continue;
			}
		if ( !strncmp(cp.cp_text,".b",2) )
			{ /* bell all users */
			system("/usr/bin/wall < /usr/lib/chat_msg");
			msg("--- back in chat ---\n");
			continue;
			}
		if ( !strncmp(cp.cp_text,".h",2) )
			{ /* get help */
			help();
			continue;
			}
		if ( !strncmp(cp.cp_text,".q",2) )
			break;
		if ( !strncmp(cp.cp_text,".x",2) )
			break;
		if ( !strncmp(cp.cp_text,"--!",3) )
			{ /* shell-escape. you might want to close it */
			msg("Function is being disabled by default\n");
			continue;
			signal(SIGINT,SIG_DFL);
			/* system(cp.cp_text+3); */
			signal(SIGINT,(void *)log_off);
			msg("--- back in chat ---\n");
			continue;
			}
		if ( !strncmp(cp.cp_text,".so",3) )
			{ /* source from a file. */
			int i, sfd;
			msg("Function is being disabled by default\n");
			continue;
			for ( i=3; i<TEXTLEN; i++ )
				if ( cp.cp_text[i] != ' ' ) break;
			sfd = open(cp.cp_text+i,0);
			if ( sfd<0 ) msg("cannot open.\n");
			else process(sfd), close(sfd), msg("done.\n");
			continue;
			}
		/* else send text to server */
		write(pipet,&cp,sizeof(cp));
		}
	}

main()
	{
	catch(SIGALRM);   /* as i do timeouts on several system calls */
	signal(SIGTERM,(void *)log_off);
	signal(SIGINT,(void *)log_off);
	signal(SIGHUP,(void *)log_off);
	parent = getpid();
	msg("Welcome to the original, evergreen\n");
	msg("Conference-utility \"CHAT\"  ver. 3.1\n");
	msg("Copyright (C) 1987 Hans Korneder, Muenchen\n");
	get_user();       /* get user-name & nick-name                */
	get_pipes();      /* get pipes to & from server working       */
	child=fork();     /* create second process                    */
	if ( child<0 ) bad("cannot create second process.\n");
	if ( !child )     /* in the child-process:                    */
		{
		int nbytes, pipef;
		signal(SIGTERM,SIG_DFL);
		signal(SIGHUP,SIG_DFL);
		signal(SIGINT,SIG_IGN);
		alarm(5);
		pipef = open(pfrom,0);
		alarm(0);
		if ( pipef<0 )
			{
			msg("open of answerpipe failed.\n");
			kill(parent,SIGTERM);  /* interrupt parent    */
			exit(2);
			}
		msg("Connection established.\nGet help with:    .h\n");
		while ( (nbytes=read(pipef,buf,sizeof(buf)))>0 )
			write(1,buf,nbytes);
		exit(0);
		}
	else /* in the parent */
		{
		msg("Waiting for connection.\n");
		alarm(3); pause();    /* give child time to open pipe */
		log_on();             /* tell the server, we are here */
		sprintf(cp.cp_magic,"%5d,",UMAGIC);  /* text messages  */
		sprintf(cp.cp_pid  ,"%5d,",parent);
		process(0);           /* do the std-in                */
		log_off();            /* tell the server, we're gone. */
		}
	}

get_pipes()
	{
	char *home;
	static char fromserv[40];
	alarm(4);
	pipet=open(SERVER_PIPE,1);
	alarm(0);
	if ( pipet < 0 ) bad("cannot open server-pipe.\n");

	home=getenv("HOME");
	/*   due to some stupid 'shox'
	if ( home ) strcpy(fromserv,home);
	else     */ strcpy(fromserv,"/tmp");
	strcat(fromserv,"/chat_XXXXXX");
	pfrom = mktemp(fromserv);
	umask (0);
	if ( mknod(pfrom,010400,0) ) bad("can't create answer-pipe.\n");
	}

get_user()
	{
	char *log;
	struct passwd *pw;
	pw = getpwuid(geteuid());
	endpwent();
	if ( pw ) strncpy(logname,pw->pw_name,8);
	else      sprintf(logname,"%-8d",geteuid());
	filter(logname);

	log=getenv("LOGNAME");
	if ( log ) strncpy(nickname,log,32);
	else       sprintf(nickname,"%-.32s",logname);
	filter(nickname);
/*
 *	venta: in order to know where a user is coming from, add the FROM
 *	shell script to the profile..
 */
	who_am_i();
	if ( frm ) strncpy(from,frm,16);
	else       strcpy(from,"somewhere");
	if ( from[0] == ':' ) strcpy(from,"Xlocalhost");
	if ( from[0] == '\0' ) strcpy(from,"localhost");
	filter(from);
	}


/*
 * Quick&Dirty who_am_i  patched from GNU sh_utils and cleaned up a lot by venta@i2ack
 * This code goes under the usual GNU-GPL (i think). You will find GNU Copyrights
 * all around your filesystem, since you are running Linux! 
 * Please #include "/usr/src/linux/COPYING"
 */

#define STRUCT_UTMP struct utmp
#define MAXHOSTNAMELEN 64
static  STRUCT_UTMP *utmp_contents;

static int read_utmp ();
static STRUCT_UTMP *search_entries ();
/*
who_am_i ();
*/

static int
read_utmp ()
{
  FILE *utmp;
  struct stat file_stats;
  int n_read;
  size_t size;

  utmp = fopen ("/etc/utmp", "r");
  if (utmp == NULL)
    fprintf(stderr,"Cannot read_utmp()\n");

  fstat (fileno (utmp), &file_stats);
  size = file_stats.st_size;
  if (size > 0)
    utmp_contents = (STRUCT_UTMP *) xmalloc (size);
  else
    {
      fclose (utmp);
      return 0;
    }

  /* Use < instead of != in case the utmp just grew.  */
  n_read = fread (utmp_contents, 1, size, utmp);
  if (ferror (utmp) || fclose (utmp) == EOF
      || n_read < size)
    printf("read_utmp() error\n");

  return size / sizeof (STRUCT_UTMP);
}


/* Search `utmp_contents', which should have N entries, for
   an entry with a `ut_line' field identical to LINE.
   Return the first matching entry found, or NULL if there
   is no matching entry. */

static STRUCT_UTMP *
search_entries (n, line)
     int n;
     char *line;
{
  register STRUCT_UTMP *this = utmp_contents;

  while (n--)
    {
      if (this->ut_name[0]
	  && this->ut_type == USER_PROCESS
	  && !strncmp (line, this->ut_line, sizeof (this->ut_line)))
	return this;
      this++;
    }
  return NULL;
}

/* Display the entry in utmp file FILENAME for this tty on standard input,
   or nothing if there is no entry for it. */

who_am_i ()
{
  register STRUCT_UTMP *utmp_entry;
  char hostname[MAXHOSTNAMELEN + 1];
  char *tty;

  if (gethostname (hostname, MAXHOSTNAMELEN + 1))
    *hostname = 0;

  tty = (char *)ttyname (0);
  if (tty == NULL)
    return;
  tty += 5;			/* Remove "/dev/".  */
  
  utmp_entry = search_entries (read_utmp(), tty);
  if (utmp_entry == NULL)
    return;

	frm=utmp_entry->ut_host;
}


#define VOID void
VOID *malloc ();
VOID *realloc ();
void free ();

#define EXIT_FAILURE 1

/* Exit value when the requested amount of memory is not available.
   The caller may set it to some other value.  */
int xmalloc_exit_failure = EXIT_FAILURE;

static VOID *
fixup_null_alloc (n)
     size_t n;
{
  VOID *p;

  p = 0;
  if (n == 0)
    p = malloc ((size_t) 1);
  if (p == 0)
    printf("memory exhausted\n");
  return p;
}

/* Allocate N bytes of memory dynamically, with error checking.  */

xmalloc (n)
     size_t n;
{
  VOID *p;

  p = malloc (n);
  if (p == 0)
    p = fixup_null_alloc (n);
  return p;
}

/* Change the size of an allocated block of memory P to N bytes,
   with error checking.
   If P is NULL, run xmalloc.  */

VOID *
xrealloc (p, n)
     VOID *p;
     size_t n;
{
  if (p == 0)
    return (VOID *)xmalloc (n);
  p = realloc (p, n);
  if (p == 0)
    p = fixup_null_alloc (n);
  return p;
}
