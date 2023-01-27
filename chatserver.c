
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include "common.h"

typedef struct _member
{
	char *name;
	int sock;
	int grid;
	struct _member *next;
	struct _member *prev;

} Member;

typedef struct _group
{
	char *name;
	int capa;
	int occu;
	struct _member *mems;

} Group;

Group *group;
int ngroups;

int findgroup(char *name)
{
	int grid;

	for (grid = 0; grid < ngroups; grid++)
	{
		if (strcmp(group[grid].name, name) == 0)
			return (grid);
	}
	return (-1);
}

Member *findmemberbyname(char *name)
{
	int grid;

	for (grid = 0; grid < ngroups; grid++)
	{
		Member *memb;

		for (memb = group[grid].mems; memb; memb = memb->next)
		{
			if (strcmp(memb->name, name) == 0)
				return (memb);
		}
	}
	return (NULL);
}

Member *findmemberbysock(int sock)
{
	int grid;

	for (grid = 0; grid < ngroups; grid++)
	{
		Member *memb;

		for (memb = group[grid].mems; memb; memb = memb->next)
		{
			if (memb->sock == sock)
				return (memb);
		}
	}
	return (NULL);
}

void cleanup()
{
	char linkname[MAXNAMELEN];

	sprintf(linkname, "%s/%s", getenv("HOME"), PORTLINK);
	unlink(linkname);
	exit(0);
}

main(int argc, char *argv[])
{
	int servsock;
	int maxsd;
	fd_set livesdset, tempset;

	if (argc != 2)
	{
		fprintf(stderr, "usage : %s <groups-file>\n", argv[0]);
		exit(1);
	}

	if (!initgroups(argv[1]))
		exit(1);

	signal(SIGTERM, cleanup);
	signal(SIGINT, cleanup);

	servsock = startserver();
	if (servsock == -1)
		exit(1);

	maxsd = servsock;

	FD_ZERO(&livesdset);
	FD_ZERO(&tempset);
	FD_SET(servsock, &livesdset);

	while (1)
	{
		int sock;
		tempset = livesdset;

		select(maxsd + 1, &tempset, NULL, NULL, NULL);

		for (sock = 3; sock <= maxsd; sock++)
		{
			if (sock == servsock)
				continue;

			if (FD_ISSET(sock, &tempset))
			{
				Packet *pkt;

				pkt = recvpkt(sock);

				if (!pkt)
				{
					char *clientname;

					socklen_t len;
					struct sockaddr_in addr;
					len = sizeof(addr);
					if (getpeername(sock, (struct sockaddr *)&addr, &len) == 0)
					{
						struct sockaddr_in *s = (struct sockaddr_in *)&addr;
						struct hostent *he;
						he = gethostbyaddr(&s->sin_addr, sizeof(struct in_addr), AF_INET);
						clientname = he->h_name;
					}
					else
						printf("Cannot get peer name");

					printf("admin: disconnect from '%s' at '%d'\n",
						   clientname, sock);

					leavegroup(sock);

					close(sock);

					FD_CLR(sock, &livesdset);
				}
				else
				{
					char *gname, *mname;

					switch (pkt->type)
					{
					case LIST_GROUPS:
						listgroups(sock);
						break;
					case JOIN_GROUP:
						gname = pkt->text;
						mname = gname + strlen(gname) + 1;
						joingroup(sock, gname, mname);
						break;
					case LEAVE_GROUP:
						leavegroup(sock);
						break;
					case USER_TEXT:
						relaymsg(sock, pkt->text);
						break;
					}

					freepkt(pkt);
				}
			}
		}

		struct sockaddr_in remoteaddr;
		socklen_t addrlen;

		if (FD_ISSET(servsock, &tempset))
		{
			int csd;

			addrlen = sizeof remoteaddr;
			csd = accept(servsock, (struct sockaddr *)&remoteaddr, &addrlen);

			if (csd != -1)
			{
				char *clientname;

				struct hostent *h;
				h = gethostbyaddr((char *)&remoteaddr.sin_addr.s_addr,
								  sizeof(struct in_addr), AF_INET);

				if (h != (struct hostent *)0)
					clientname = h->h_name;
				else
					printf("gethostbyaddr failed\n");

				printf("admin: connect from '%s' at '%d'\n",
					   clientname, csd);

				FD_SET(csd, &livesdset);

				if (csd > maxsd)
					maxsd = csd;
			}
			else
			{
				perror("accept");
				exit(0);
			}
		}
	}
}

int initgroups(char *groupsfile)
{
	FILE *fp;
	char name[MAXNAMELEN];
	int capa;
	int grid;

	fp = fopen(groupsfile, "r");
	if (!fp)
	{
		fprintf(stderr, "error : unable to open file '%s'\n", groupsfile);
		return (0);
	}

	fscanf(fp, "%d", &ngroups);

	group = (Group *)calloc(ngroups, sizeof(Group));
	if (!group)
	{
		fprintf(stderr, "error : unable to calloc\n");
		return (0);
	}

	for (grid = 0; grid < ngroups; grid++)
	{
		if (fscanf(fp, "%s %d", name, &capa) != 2)
		{
			fprintf(stderr, "error : no info on group %d\n", grid + 1);
			return (0);
		}

		group[grid].name = strdup(name);
		group[grid].capa = capa;
		group[grid].occu = 0;
		group[grid].mems = NULL;
	}
	return (1);
}

int listgroups(int sock)
{
	int grid;
	char pktbufr[MAXPKTLEN];
	char *bufrptr;
	long bufrlen;

	bufrptr = pktbufr;
	for (grid = 0; grid < ngroups; grid++)
	{
		sprintf(bufrptr, "%s", group[grid].name);
		bufrptr += strlen(bufrptr) + 1;

		sprintf(bufrptr, "%d", group[grid].capa);
		bufrptr += strlen(bufrptr) + 1;

		sprintf(bufrptr, "%d", group[grid].occu);
		bufrptr += strlen(bufrptr) + 1;
	}
	bufrlen = bufrptr - pktbufr;

	sendpkt(sock, LIST_GROUPS, bufrlen, pktbufr);
	return (1);
}

int joingroup(int sock, char *gname, char *mname)
{
	int grid;
	Member *memb;

	grid = findgroup(gname);
	if (grid == -1)
	{
		char *errmsg = "no such group";
		sendpkt(sock, JOIN_REJECTED, strlen(errmsg), errmsg);
		return (0);
	}

	memb = findmemberbyname(mname);

	if (memb)
	{
		char *errmsg = "member name already exists";
		sendpkt(sock, JOIN_REJECTED, strlen(errmsg), errmsg);
		return (0);
	}

	if (group[grid].capa == group[grid].occu)
	{
		char *errmsg = "room is full";
		sendpkt(sock, JOIN_REJECTED, strlen(errmsg), errmsg);
		return (0);
	}

	memb = (Member *)calloc(1, sizeof(Member));
	if (!memb)
	{
		fprintf(stderr, "error : unable to calloc\n");
		cleanup();
	}
	memb->name = strdup(mname);
	memb->sock = sock;
	memb->grid = grid;
	memb->prev = NULL;
	memb->next = group[grid].mems;
	if (group[grid].mems)
	{
		group[grid].mems->prev = memb;
	}
	group[grid].mems = memb;
	printf("admin: '%s' joined '%s'\n", mname, gname);

	group[grid].occu++;

	sendpkt(sock, JOIN_ACCEPTED, 0, NULL);
	return (1);
}

int leavegroup(int sock)
{
	Member *memb;

	memb = findmemberbysock(sock);
	if (!memb)
		return (0);

	if (memb->next)
		memb->next->prev = memb->prev;

	/* remove from ... */
	if (group[memb->grid].mems == memb)
		group[memb->grid].mems = memb->next;

	else
		memb->prev->next = memb->next;

	printf("admin: '%s' left '%s'\n",
		   memb->name, group[memb->grid].name);

	group[memb->grid].occu--;

	free(memb->name);
	free(memb);
	return (1);
}

int relaymsg(int sock, char *text)
{
	Member *memb;
	Member *sender;
	char pktbufr[MAXPKTLEN];
	char *bufrptr;
	long bufrlen;

	sender = findmemberbysock(sock);
	if (!sender)
	{
		fprintf(stderr, "strange: no member at %d\n", sock);
		return (0);
	}

	bufrptr = pktbufr;
	strcpy(bufrptr, sender->name);
	bufrptr += strlen(bufrptr) + 1;
	strcpy(bufrptr, text);
	bufrptr += strlen(bufrptr) + 1;
	bufrlen = bufrptr - pktbufr;

	for (memb = group[sender->grid].mems; memb; memb = memb->next)
	{
		if (memb->sock == sock)
			continue;
		sendpkt(memb->sock, USER_TEXT, bufrlen, pktbufr);
	}
	printf("%s: %s", sender->name, text);
	return (1);
}
