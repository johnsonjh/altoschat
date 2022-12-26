/*
	This is the include-file for both the chat-user-
	interface and the chat-server. Modifications can
	be done -- dont forget to recompile both the
	server and the user-interface ( or use 'make' )
	Last revision: Hans Korneder, May 1987
*/

/*
	NUSERS reflects the table-size within the server.
	It's the maximum number of user-interfaces, that can
	be served from the server. The tables are searched
	sequentially -- increasing NUSERS might slow down
	the server.
*/
#define NUSERS  30

/*
	TEXTLEN reflects the maximum number of characters
	that can be transferred within one chat-packet.
	it should be at least 96 ( 8+32+40+16 within the
	startup-packet for: logname, nickname, pfrom, from )
	Increasing the TEXTLEN above 118 might cause problems
	for x.25-users: the packetlength will exceed 128,
	thus more than one x.25-packet has to be sent
	for one chat-packet.
*/
#define TEXTLEN 118

/*
	Some magic numbers: They are used to identify incoming
	packets to the server ( and to reject garbage being
	sent to the server from a hacker's program )
	The magic numbers are stored in signed short integers,
	and they must be unique.
*/
#define UMAGIC1 27001    /* user calls for the first time */
#define UMAGICL 27002    /* user calls for the last  time */
#define UMAGIC  27003    /* user sends some text          */

/*
	The packet, that is sent from the user-interface
	to the server. The magic number and the pid are
	transferred as ascii-characters. i once had a
	version running, using binary figures...
	... and then someone came and wanted to connect
	    a Motorola to an Intel machine
	... and then someone came, and just had a 7-bit-line
	... and then i gave it up

	use even number of bytes for cp_magic and cp_pid
	( as some compilers do word-alignments for structure-
	members. yes, it's true! )
*/
struct chat_packet
	{
	char cp_magic[6];      /* magic number of the packet          */
	char cp_pid[6];        /* pid-- to identify sender            */
	char cp_text[TEXTLEN]; /* the text                            */
	} ;

/*
	SERVER_PIPE is the name of a fifo-file, that must be
	writable by everybody ( to transfer packets to the 
	server ). Thus the directories on the path to it
	must have at least the x-flag set for everybody.
*/
#define SERVER_PIPE "/usr/lib/chat_server"
