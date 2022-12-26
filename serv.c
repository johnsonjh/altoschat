/*
	The server-process for the chat-program.
	The server gets its messages from the SERVER_PIPE
	interprets several commands & transfers text.
	Last modification: May 1987, Hans Korneder
*/

#include <signal.h>
#include <fcntl.h>
#include "chat.h"

/* external declarations */
extern int   optind, getopt();
extern char *optarg, *ctime(), *strchr();
extern long  time(), atol();

/* global variables */
struct users
	{
	char us_flag;                /* 0=empty, 1=user, 2=system     */
	int  us_pid;                 /* pid of user                   */
	char us_logname[8+1];        /* name (from /etc/passwd)       */
	char us_nickname[32+1];      /* the user's nick-name          */
	long us_channel;             /* channel number ( positive )   */
	char us_answer[40+1];        /* answer-pipe                   */
	char us_from[16+1];          /* the user's origin             */
	int  us_fd;                  /* filedescriptor for answerpipe */
	} users[NUSERS];
struct chans
	{
	long ch_chan;                /* channel number                */
	char ch_pass[8+1];           /* channel passwd                */
	} chans[NUSERS];
struct chat_packet cp;               /* incoming data packets         */
char buf[512];                       /* some data buffer              */
int input;                           /* incoming pipefile-descriptor  */
char *logfile=0;                     /* logging the text              */
char *dbgfile=0;                     /* logging all transactions      */
int log,dbg;                         /* the file-descriptors          */
long t;                              /* time                          */

catch(s)
int s;
	{
	signal(s,(void *)catch);
	to_dbg("signal %d catched.\n",s);
	}

bad(text)
char *text;
	{
	write(2,text,strlen(text));
	exit(1);
	}

fill(text,size,val)
char *text, val;
int size;
	{
	while ( size-- ) *text++ = val;
	}

blankstrip(text,size)
char *text;
int size;
	{
	while ( size-- )
		if ( text[size] != ' ' ) break;
		else text[size]=0;
	}

/*VARARGS*/
to_log(p1,p2,p3,p4,p5,p6,p7,p8,p9)
char *p1,*p2,*p3,*p4,*p5,*p6,*p7,*p8,*p9;
	{
	char buf[512];
	if ( logfile || dbgfile )
		{
		sprintf(buf,p1,p2,p3,p4,p5,p6,p7,p8,p9);
		if ( logfile ) write(log,buf,strlen(buf));
		if ( dbgfile ) to_dbg("Log: %s",buf);
		}
	}

/*VARARGS*/
to_dbg(p1,p2,p3,p4,p5,p6,p7,p8,p9)
char *p1,*p2,*p3,*p4,*p5,*p6,*p7,*p8,*p9;
	{
	char buf[512];
	if ( dbgfile )
		{
		sprintf(buf,p1,p2,p3,p4,p5,p6,p7,p8,p9);
		write(dbg,buf,strlen(buf));
		}
	}

int identify(pid)
int pid;
	{
	int i;
	for(i=0; i<NUSERS; i++)
		if ( (users[i].us_flag) && (users[i].us_pid==pid) )
			return i;
	return -1;
	}

check_free(ch)
long ch;
	{ /* check for pending passwds */
	int k,n;
	for ( k=n=0; k<NUSERS; k++)
		if ( users[k].us_flag && users[k].us_channel==ch ) n++;
	to_dbg("Check-free [%ld]: %d users\n",ch,n);
	if ( n>0 ) return;
	for(k=0; k<NUSERS; k++)
		if ( chans[k].ch_chan==ch ) chans[k].ch_chan= -1L;
	}

log_off(i)
int i;
	{
	int k;
	to_dbg("log_off called for %d\n",i);
	users[i].us_flag = 0;
	close(users[i].us_fd);
	unlink(users[i].us_answer); /* just to be sure.. */
	sprintf(buf,"---%s---\n",users[i].us_nickname);
	to_log("%s",buf);
	for(k=0; k<NUSERS; k++) write_msg(k,-1L,buf);
	check_free(users[i].us_channel);
	}

main(argc,argv)
int argc;
char *argv[];
	{
	int c, i, errcnt=0;

	setpgrp(); /* ignore init-mode-changes */
	if ( geteuid() )
		bad("not super-user\n");

	/* process arguments */
	while ( (c=getopt(argc,argv,"l:d:")) != (-1) )
		switch ( c )
			{
			case 'l': logfile=optarg; break;
			case 'd': dbgfile=optarg; break;
			case '?': errcnt++;
			}
	if ( errcnt ) bad("usage: serv [-d debugfile] [-l logfile]\n");

	/* process log-files */
	umask (0);
	if ( dbgfile )
		{
		dbg = open(dbgfile,O_WRONLY|O_TRUNC|O_CREAT,0600);
		if ( dbg<0 ) bad("cannot open dbgfile.\n");
		}
	if ( logfile )
		{
		log = open(logfile,O_WRONLY|O_APPEND|O_CREAT,0600);
		if ( log<0 ) bad("cannot open logfile.\n");
		}

	t =time((long *)0);
	to_log("Chat-Server Starting At: %.24s\n",ctime(&t));

	for(i=0; i<NUSERS; i++)
		users[i].us_flag=0,
		chans[i].ch_chan= -1L;

	catch(SIGPIPE); /* when a chat-child dies.. */
	catch(SIGALRM); /* as i do some timeouts on system calls */

	/* all preparations done, get work.. */
	for (;;)
		{
		input=open(SERVER_PIPE,O_RDONLY);
		if ( input<0 ) bad("cannot open server-pipe.\n");
		while ( read(input,&cp,sizeof(cp))>0 )
			process(); /* work on packet. */
		/* last chat-user went out. go sleeping.. */
		close(input);
		}
	}

write_msg(person,channel,text)
int person;
long channel;
char *text;
	{
	int ret_code;
	if ( person<0 || person>=NUSERS ) return;
	if ( ! users[person].us_flag ) return ;
	to_dbg("write_msg:%d,%ld,%s",person,channel,text);
	/* send to person, but only if on specified channel */
	if ( channel>=0 && channel!=users[person].us_channel ) return;

	alarm(2); /* time-out the write. slow users might be thrown out. */
	ret_code=write(users[person].us_fd,text,strlen(text));
	alarm(0);
	if ( ret_code <=0 )
		log_off(person);
	}

process()
	{
	int pid, i, k;
	int magic;
	to_dbg("Got Packet: \"%s\"\n",&cp);
	sscanf(cp.cp_magic,"%6d",&magic);
	switch ( magic )
		{
		case UMAGIC1: /* user first time call */
		case UMAGICL: /* user last time call  */
		case UMAGIC : /* user text */
			sscanf(cp.cp_pid,"%6d",&pid);
			switch ( magic )
				{
				case UMAGIC1: /* user first time call */
					/* find free slot */
					for(i=0; i<NUSERS; i++)
						if ( !users[i].us_flag ) break;
					if ( i==NUSERS )
						{
						to_dbg("too many entries - ignoring request\n");
						break;
						}
					to_dbg("preparing new entry %d; pid=%d\n",i,pid);
					fill(users+i,sizeof(struct users),0);
					users[i].us_pid = pid;
					strncpy(users[i].us_logname ,cp.cp_text        , 8);
					strncpy(users[i].us_nickname,cp.cp_text+8      ,32);
					strncpy(users[i].us_answer  ,cp.cp_text+8+32   ,40);
					strncpy(users[i].us_from    ,cp.cp_text+8+32+40,16);
					blankstrip(users[i].us_nickname,32);
					blankstrip(users[i].us_answer  ,40);
					alarm(2); /* some users rush out... */
					users[i].us_fd = open(users[i].us_answer,1);
					alarm(0);
					if ( users[i].us_fd < 0 )
						{
						to_dbg("cannot open answerpipe \"%s\"\n",
							users[i].us_answer);
						break;
						}
					users[i].us_flag = 1;
					to_dbg("Created entry #%d.\n",i);
					t = time((long *)0);
					to_log("Incoming Call From %s At %24.24s\n",
						users[i].us_from,ctime(&t));
					sprintf(buf,"+++%s+++\n",users[i].us_nickname);
					to_log("%s",buf);
					for(k=0; k<NUSERS; k++) write_msg(k,-1L,buf);
					break;
				case UMAGICL: /* user last time call  */
					i = identify(pid);
					if ( i<0 )
						{
						to_dbg("unknown pid: %d -- ignoring.\n",pid);
						break;
						}
					log_off(i);
					break;
				case UMAGIC : /* user text */
					i = identify(pid);
					if ( i<0 )
						{
						to_dbg("unknown pid: %d -- ignoring.\n",pid);
						break;
						}
					blankstrip(cp.cp_text,TEXTLEN);
					if ( ! strlen(cp.cp_text) ) break;
					/* process commands ( like .s, !  etc  ) */
					if ( !strncmp(cp.cp_text,".n ",3) )
						{
						sprintf(buf,">>>%s>>>%s>>>\n",
							users[i].us_nickname, cp.cp_text+3);
						to_log("%s",buf);
						for(k=0; k<NUSERS; k++)
							write_msg(k,users[i].us_channel,buf);
						strncpy(users[i].us_nickname,cp.cp_text+3,32);
						break;
						}
					if ( !strncmp(cp.cp_text,".s",2) )
						{
						write_msg(i,-1L,
							"No Chan       From             User     Called\n");
						for(k=0; k<NUSERS; k++)
							if ( users[k].us_flag )
								{
								sprintf(buf,"%2d %10ld %s %s %s\n",k,
									users[k].us_channel,
									users[k].us_from,
									users[k].us_logname,
									users[k].us_nickname);
								write_msg(i,-1L,buf);
								}
						break;
						}
					if ( !strncmp(cp.cp_text,".p ",3) )
						{
						int n;
						if ( ! users[i].us_channel )
							{
							write_msg(i,-1L,
								"Channel 0 cannot be protected.\n");
							break;
							}
						for ( k=n=0; k<NUSERS; k++ )
						if ( users[k].us_flag &&
						users[k].us_channel==users[i].us_channel ) n++;
						if ( n>1 )
							{
							write_msg(i,-1L,
								"You are not alone on the channel.\n");
							break;
							}
						for(k=0; k<NUSERS; k++)
							if ( chans[k].ch_chan==users[i].us_channel )
								break;
						if ( k==NUSERS )
							for(k=0; k<NUSERS; k++)
								if ( chans[k].ch_chan<0 ) break;
						fill(chans+k,sizeof(struct chans),0);
						chans[k].ch_chan = users[i].us_channel;
						strncpy(chans[k].ch_pass,cp.cp_text+3,8);
						to_dbg("Protecting chan %ld with %s\n",
							chans[k].ch_chan, chans[k].ch_pass);
						break;
						}
					if ( !strncmp(cp.cp_text,".c",2) )
						{
						long old,new;
						old = users[i].us_channel;
						new = atol(cp.cp_text+2);
						if ( new<0 ) new = -new;
						if ( old == new ) break;
						for(k=0; k<NUSERS; k++)
							if ( chans[k].ch_chan==new ) break;
						if ( k<NUSERS ) /* check passwd */
							{
							char *pw;
							pw = strchr(cp.cp_text,',');
							if ( !pw )
								{
								write_msg(i,-1L,
								"Channel is protected. Use a passwd\n");
								break;
								}
							if ( strncmp(chans[k].ch_pass,pw+1,8) )
								{
								write_msg(i,-1L,"Wrong passwd.\n");
								break;
								}
							}
						sprintf(buf,">>>%s>>> chan %ld\n",
							users[i].us_nickname,new);
						for(k=0; k<NUSERS; k++)
							write_msg(k,old,buf),
							write_msg(k,new,buf);
						to_log("%s",buf);
						users[i].us_channel=new;
						check_free(old);
						break;
						}
					if ( !strncmp(cp.cp_text,"!",1) )
						{
						k=atol(cp.cp_text+1);
						sprintf(buf,"%s whispers: %s\n",
							users[i].us_nickname,cp.cp_text);
						to_log("[%d:%ld]%s",i,users[i].us_channel,buf);
						write_msg(k,-1L,buf);
						break;
						}
					/* plain text to be transmitted.. */
					sprintf(buf,"%s says: %s\n",
						users[i].us_nickname,cp.cp_text);
					to_log("[%d:%ld]%s",i,users[i].us_channel,buf);
					for(k=0; k<NUSERS; k++) if ( i!=k )
						write_msg(k,users[i].us_channel,buf);
					break;
				}
			break;
		default: to_dbg("unknown magic number\n");
		}
	}
