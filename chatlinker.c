/* ���ӷ������Ϳͻ����ĺ��� */

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>
#include "common.h"

/*
  Ϊ���������տͻ���������׼����
  ��ȷ���� socket �ļ�������
  ���󷵻� -1
*/
int startserver()
{
  int sd;             /* socket ������ */
  int myport;         /* �������˿� */
  const char *myname; /* ����������ȫ�� */

  char linktrgt[MAXNAMELEN];
  char linkname[MAXNAMELEN];

  /*
  ���� socket �������� TCP socket ������
  */
  sd = socket(PF_INET, SOCK_STREAM, 0);

  /*
    ����bind������һ�����ص�ַָ�ɸ� socket
  */

  struct sockaddr_in server_address;
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = htonl(INADDR_ANY); /* ͨ���ַ INADDR_ANY ��ʾIP��ַΪ 0.0.0.0��
                            �ں����׽��ֱ����Ӻ�ѡ��һ�����ص�ַ
                            htonl���� ���ڽ� INADDR_ANY ת��Ϊ�����ֽ��� */
  server_address.sin_port = htons(0);                 /* ָ��Ϊͨ��˿� 0������ bind �������ں˽�����ѡ��һ����ʱ�˿� */

  bind(sd, (struct sockaddr *)&server_address, sizeof(server_address));

  /* ����listen ���������� socket ������ sd ����Ϊ�����ؼ���״̬�������ý��ܶ��еĳ���Ϊ20 */
  listen(sd, 20);

  /*
    ���� getsockname��gethostname �� gethostbyname ȷ�������������ͷ������˿ں�
  */

  char hostname[MAXNAMELEN];

  if (gethostname(hostname, sizeof hostname) != 0)
    perror("gethostname");

  struct hostent *h;
  h = gethostbyname(hostname);

  int len = sizeof(struct sockaddr);

  getsockname(sd, (struct sockaddr *)&server_address, &len);

  myname = h->h_name;
  myport = ntohs(server_address.sin_port);

  /* �ڼ�Ŀ¼�´�����������'.chatport'ָ��linktrgt */
  sprintf(linktrgt, "%s:%d", myname, myport);
  sprintf(linkname, "%s/%s", getenv("HOME"), PORTLINK); /* ��ͷ�ļ� common.h �У�
                            #define PORTLINK ".chatport" */
  if (symlink(linktrgt, linkname) != 0)
  {
    fprintf(stderr, "error : server already exists\n");
    return (-1);
  }

  /* ׼�����ܿͻ������� */
  printf("admin: started server on '%s' at '%d'\n",
         myname, myport);
  return (sd);
}

/*
  �ͷ������������ӣ���ȷ���� socket ��������
  ʧ�ܷ���  -1
*/
int hooktoserver()
{
  int sd;

  char linkname[MAXNAMELEN];
  char linktrgt[MAXNAMELEN];
  char *servhost;
  char *servport;
  int bytecnt;

  /* ��ȡ��������ַ */
  sprintf(linkname, "%s/%s", getenv("HOME"), PORTLINK);
  bytecnt = readlink(linkname, linktrgt, MAXNAMELEN);
  if (bytecnt == -1)
  {
    fprintf(stderr, "error : no active chat server\n");
    return (-1);
  }

  linktrgt[bytecnt] = '\0';

  /* ��÷����� IP ��ַ�Ͷ˿ں� */
  servport = index(linktrgt, ':');
  *servport = '\0';
  servport++;
  servhost = linktrgt;

  /* ��÷����� IP ��ַ�� unsigned short ��ʽ */
  unsigned short number = (unsigned short)strtoul(servport, NULL, 0);

  /*
  ���ú��� socket ���� TCP �׽���
  */

  sd = socket(AF_INET, SOCK_STREAM, 0);

  /*
  ���� gethostbyname() �� connect()���� 'servhost' �� 'servport' �˿�
  */
  struct hostent *hostinfo;
  struct sockaddr_in address;

  hostinfo = gethostbyname(servhost); /* �õ������������� */
  address.sin_addr = *(struct in_addr *)*hostinfo->h_addr_list;
  address.sin_family = AF_INET;
  address.sin_port = htons(number);

  if (connect(sd, (struct sockaddr *)&address, sizeof(address)) < 0)
  {
    perror("connecting");
    exit(1);
  }

  /* ���ӳɹ� */
  printf("admin: connected to server on '%s' at '%s'\n",
         servhost, servport);
  return (sd);
}

/* ���ں˶�ȡһ���׽��ֵ���Ϣ */
int readn(int sd, char *buf, int n)
{
  int toberead;
  char *ptr;

  toberead = n;
  ptr = buf;
  while (toberead > 0)
  {
    int byteread;

    byteread = read(sd, ptr, toberead);
    if (byteread <= 0)
    {
      if (byteread == -1)
        perror("read");
      return (0);
    }

    toberead -= byteread;
    ptr += byteread;
  }
  return (1);
}

/* �������ݰ� */
Packet *recvpkt(int sd)
{
  Packet *pkt;

  /* ��̬�����ڴ� */
  pkt = (Packet *)calloc(1, sizeof(Packet));
  if (!pkt)
  {
    fprintf(stderr, "error : unable to calloc\n");
    return (NULL);
  }

  /* ��ȡ��Ϣ���� */
  if (!readn(sd, (char *)&pkt->type, sizeof(pkt->type)))
  {
    free(pkt);
    return (NULL);
  }

  /* ��ȡ��Ϣ���� */
  if (!readn(sd, (char *)&pkt->lent, sizeof(pkt->lent)))
  {
    free(pkt);
    return (NULL);
  }
  pkt->lent = ntohl(pkt->lent);

  /* Ϊ��Ϣ���ݷ���ռ� */
  if (pkt->lent > 0)
  {
    pkt->text = (char *)malloc(pkt->lent);
    if (!pkt)
    {
      fprintf(stderr, "error : unable to malloc\n");
      return (NULL);
    }

    /* ��ȡ��Ϣ�ı� */
    if (!readn(sd, pkt->text, pkt->lent))
    {
      freepkt(pkt);
      return (NULL);
    }
  }
  return (pkt);
}

/* �������ݰ� */
int sendpkt(int sd, char typ, long len, char *buf)
{
  char tmp[8];
  long siz;

  /* �Ѱ������ͺͳ���д���׽��� */
  bcopy(&typ, tmp, sizeof(typ));
  siz = htonl(len);
  bcopy((char *)&siz, tmp + sizeof(typ), sizeof(len));
  write(sd, tmp, sizeof(typ) + sizeof(len));

  /* ����Ϣ�ı�д���׽��� */
  if (len > 0)
    write(sd, buf, len);
  return (1);
}

/* �ͷ����ݰ�ռ�õ��ڴ�ռ� */
void freepkt(Packet *pkt)
{
  free(pkt->text);
  free(pkt);
}
