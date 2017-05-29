#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <math.h>
#define BUFSIZE 2048
#define QLEN 5

fd_set lockFD;
fd_set regAll;
fd_set rfds;
fd_set afds;
pthread_mutex_t mutex;

typedef struct tag {
	char *name;
	fd_set clients;
	int numClients;
	struct tag * link;
} tag_t;

typedef struct args{
	int fd;
	char *buf;
} args_t;

int passivesock( char *service, char *protocol, int qlen, int *rport );

tag_t *firstTag;

tag_t* createNewTag(char* tagName, int fd, tag_t* next){
	tag_t* tag = (tag_t*)malloc(sizeof(tag_t));
	tag->name = (char*)malloc((strlen(tagName)+1)*sizeof(char));
	strcpy(tag->name, tagName);
	FD_ZERO(&tag->clients);
	FD_SET(fd, &tag->clients);
	tag->numClients = 1;
	tag->link = next;
	return tag;
}


void deregAllTags(int fd){
	pthread_mutex_lock(&mutex);
	tag_t* curTag=firstTag;
	tag_t* parentTag = NULL;
	while (curTag!=NULL){
		if (FD_ISSET(fd, &curTag->clients)){
			curTag->numClients--;
			FD_CLR(fd, &curTag->clients);
		}
		if (curTag->numClients == 0){
			tag_t* temp = curTag;
			if (parentTag == NULL){
				firstTag = curTag->link;
				curTag = firstTag;
			} else {
				parentTag->link = curTag->link;
				curTag = curTag->link;
			}
			free(temp->name);
			free(temp);
		} else {
			parentTag = curTag;
			curTag=curTag->link;
		}
	}
	pthread_mutex_unlock(&mutex);
}

void* image ( void*args ){
	tag_t *curTag = NULL;
	firstTag=NULL;
	tag_t *parentTag=NULL;
	args_t *arguments = ((args_t*)args);
	int fd = (int)arguments->fd;
	char buf[BUFSIZE];
	strcpy(buf, arguments->buf); 
	buf[5]='\0';
	int cc, i=5;
	/////read until '/' symbol////

	while (1){
		if ( (cc = read(fd, buf+i, 1)) <= 0){				  
			printf( "The client has gone.\n" );
			(void) close(fd);
			FD_CLR( fd, &afds );
			deregAllTags(fd);
			FD_CLR(fd, &regAll);	
			free(args);
			pthread_exit(NULL);		  
		}
		if (buf[i] == '/'){
			break;
		}
		i++;
	}
	buf[i+1] = '\0';

	///////define length of BIGBUFSIZE/////////
	char tag[2048]="";
	int bytecount;
	int taglen;

	///find out tag length////
	if (sscanf(buf, "%*s #%s %i/", tag, &bytecount)==2){
		taglen = strlen(tag)+2;
	} else if (sscanf(buf, "%*s %i/", &bytecount)==1){
		taglen=0;
	} else {
		printf("ERROR READING IMAGE\n");
		fflush(stdout);
	}
	int numLen=log10(bytecount)	+ 1;
	int start = 7 + taglen +numLen;
	int BIGBUFSIZE=start+bytecount;	
	char* bigBuf=(char*)malloc(BIGBUFSIZE*sizeof(char));
	printf("Client says %s\n", buf);	
	fflush(stdout);
	i=0;
	while (buf[i]!='\0'){
		bigBuf[i]=buf[i];
		printf("%c", buf[i]);
		i++;
	}
	bigBuf[i]='\0';
	///////read from fd to big buf/////////
	long int total=0;
	printf("%c\n",bigBuf[start+total]);

	do{
		cc=read(fd, &bigBuf[start+total], BUFSIZE); //+strlen(bigBuf)+1, BUFSIZE); //read from fd to bigBuf (starting from its last point)
		printf(" %c", cc);
		total+=cc;
	} 	while ( total<bytecount );

	////send to clients registered to the given tag/////
	if(sscanf(buf, "%*s #%s", tag)==1){
		pthread_mutex_lock(&mutex);
    //if it is with #, find a hashtag in the list and forward the whole msg to all of the guys in fd registred to the tag
		curTag = firstTag;
		while (curTag!=NULL){
			if (strcasecmp(tag, curTag->name)==0){
				for (i = 0; FD_SETSIZE; i++){
					if (FD_ISSET(i, &curTag->clients)){
						write(i, bigBuf, BIGBUFSIZE);
					}
				}
				break;
			} else	{
				curTag=curTag->link;
			}
		}
		pthread_mutex_unlock(&mutex);

	}
	////////send to clients registered to all//////
	for (i = 0; i<FD_SETSIZE; i++){

		if (FD_ISSET(i, &regAll)){
			pthread_mutex_lock(&mutex);
			write(i, bigBuf, BIGBUFSIZE);
			pthread_mutex_unlock(&mutex);
		}
		
	}
	pthread_mutex_lock(&mutex);
	FD_CLR(fd, &lockFD);
	pthread_mutex_unlock(&mutex);
	free(args);
	pthread_exit(NULL);
}


int main( int argc, char *argv[] ){
	char			buf[BUFSIZE];
	char			*service;
	struct sockaddr_in	fsin;
	int			msock;
	int			ssock;

	int			alen;
	int			fd;
	int			nfds;
	int			rport = 0;
	int			cc;

	switch (argc) {
		case	1:
			// No args? let the OS choose a port and tell the user
		rport = 1;
		break;
		case	2:
			// User provides a port? then use it
		service = argv[1];
		break;
		default:
		fprintf( stderr, "usage: server [port]\n" );
		exit(-1);
	}
	msock = passivesock( service, "tcp", QLEN, &rport );

	if (rport){
		//	Tell the user the selected port
		printf( "server: port %d\n", rport );
		fflush( stdout );
	}
	nfds = msock+1;

	FD_ZERO(&afds);
	FD_SET( msock, &afds );

	tag_t *curTag = NULL;
	firstTag=NULL;
	tag_t *parentTag=NULL;
	int i;
	char tag[2048];

	FD_ZERO(&regAll);

	FD_ZERO(&lockFD);

	for (;;){
		memcpy((char *)&rfds, (char *)&afds, sizeof(rfds));

		if (select(nfds, &rfds, (fd_set *)0, (fd_set *)0, (struct timeval *)0) < 0){
			fprintf( stderr, "server select: %s\n", strerror(errno) );
			exit(-1);
		}
		if (FD_ISSET( msock, &rfds)) {
			int	ssock;
			alen = sizeof(fsin);
			ssock = accept( msock, (struct sockaddr *)&fsin, &alen );
			if (ssock < 0) {
				fprintf( stderr, "accept: %s\n", strerror(errno) );
				exit(-1);
			}
			printf("Client arrived!\n");
			fflush(stdout);
			/* start listening to this guy */
			FD_SET( ssock, &afds );
			if (ssock+1>nfds){
				nfds=ssock+1;
			}
		}
		int i, cc, size;

		/*	Handle the participants requests  */
		for ( fd = 0; fd < nfds; fd++ ){
			if (fd != msock && FD_ISSET(fd, &rfds) && !FD_ISSET(fd, &lockFD)){
        // READ first 5 bytes
				if ( (cc = read(fd, buf, 5)) <= 0){
					printf( "The client has gone.\n" );
					(void) close(fd);
					FD_CLR( fd, &afds );
					deregAllTags(fd);
					FD_CLR(fd, &regAll);
				}
				buf[5] = '\0';
				//////////////////////////////// MSGE //////////////////////////////////////////				
				if (strcmp(buf, "MSGE ")==0){
					i = 5;
					while (1){
						if ((cc = read(fd, buf+i, 1)) <= 0){				  
							printf( "The client has gone.\n" );
							(void) close(fd);
							FD_CLR( fd, &afds );
							deregAllTags(fd);
							FD_CLR(fd, &regAll);			  
						}
						if (buf[i] == '/'){
							break;
						}
						i++;
					}
					buf[i+1] = '\0';
					size=i+1;
					char tag[2048]="";
					int j, bytecount;
					int taglen;
					if (sscanf(buf, "%*s #%s %i/", tag, &bytecount)==2){
						taglen = strlen(tag)+2;
					} else if (sscanf(buf, "%*s %i/", &bytecount)==1){
						taglen=0;
					} else {
						printf("ERROR READING MSGE\n");
					}
					int numLen=log10(bytecount)	+ 1;
					int start = 6 + taglen +numLen;	
					read(fd, buf+start, bytecount+1);
					size=start+bytecount;
					buf[size]='\0';
					printf("Client says %s\n", buf);	
					fflush(stdout);
					if(sscanf(buf, "%*s #%s", tag)==1){
						pthread_mutex_lock(&mutex);
		//if it is with #, find a hashtag in the list and forward the whole msg to all of the guys with fd=1 in the fdset
						curTag = firstTag;
						while (curTag!=NULL){
							if (strcasecmp(tag, curTag->name)==0){
								for (i = 0; i<nfds; i++){
									if (FD_ISSET(i, &curTag->clients)){
										write(i, buf, size);
									}
								}
								break;
							} else	{
								curTag=curTag->link;
							}
						}
						pthread_mutex_unlock(&mutex);
					}

					for (i = 0; i<nfds; i++){
						if (FD_ISSET(i, &regAll)){
							write(i, buf, size);
						}
					}
					//////////////////////////////IMAGE////////////////////////////////
				} else if (strcmp(buf, "IMAGE")==0){
				//the idea of fdset for clients working with images proposed by Makar Lezhnikov  
					printf("got here in IMAGE\n");
					pthread_mutex_lock(&mutex);
					FD_SET(fd, &lockFD);
					pthread_mutex_unlock(&mutex);
					args_t* args = (args_t*)malloc(sizeof(args_t));
					args->fd=fd;
					args->buf=buf;
					pthread_t img;
					pthread_create( &img, NULL, image, (void*)args);

						//////////////////REGISTER////////////////////////////////////

				} else {
					int i=5;
					while (1){
						if ((cc = read(fd, buf+i, 1)) <= 0){				  
							printf( "The client has gone.\n" );
							(void) close(fd);
							FD_CLR( fd, &afds );
							deregAllTags(fd);
							FD_CLR(fd, &regAll);			  
						}
						if (buf[i] == '\n' && buf[i-1] == '\r'){
							break;
						}
						i++;
					}
					buf[i+1]='\0';
					size = i+1;
					printf("Client says %s\n", buf);	
					fflush(stdout);
					if (strncmp(buf, "MSG ", 4)==0){			
						if(sscanf(buf, "%*s #%s", tag) == 1){
							pthread_mutex_lock(&mutex);
	//if it is with #, find a hashtag in the list and forward the whole msg to all clients with fd=1 in the fd set 
							curTag = firstTag;
							while (curTag!=NULL){
								if (strcasecmp(tag, curTag->name)==0){
									for (i = 0; i<nfds; i++){
										if (FD_ISSET(i, &curTag->clients)){
											write(i, buf, size);
										}
									}
									break;
								} else	{
									curTag=curTag->link;
								}
							}
							pthread_mutex_unlock(&mutex);
						}

						for ( i = 0; i<nfds; i++){
							if (FD_ISSET(i, &regAll)){
								write(i, buf, size);					
							}

						}

					} else if ( strncmp ( buf, "REGISTER ", 9 ) == 0){				
			//if a client wants to register for a specific hashtag, find that hashtag, set fd bit to 1 in the fd set					
						char tag[2048]="";			
						if (sscanf(buf, "%*s %s", tag) ==1 ){
							pthread_mutex_lock(&mutex);
							if (!FD_ISSET(fd, &regAll)){             
								curTag = firstTag;
								parentTag = NULL;
								if ( curTag == NULL ){
									firstTag=createNewTag(tag, fd, NULL);
								} else {
									while (curTag!=NULL) {
										if ( strcmp(tag, curTag->name)==0 ){
											FD_SET(fd, &curTag->clients);
											break;
										} if (curTag->link == NULL){
											curTag->link = createNewTag(tag, fd, NULL);
											break;
										}
										curTag = curTag->link;
									}
								}         
							}
							pthread_mutex_unlock(&mutex);
						}											
			////////////////////////////////////////DEREGISTER or REGISTERALL///////////////////////////////////////				
					} else if ( strncmp (buf, "DEREGISTER ", 11 ) == 0){
			//if a client wants to deregister from some hashtag, find that hashtag and set fd bit to 0
						char tag[2048]="";
						if (sscanf(buf, "%*s %s", tag) == 1){	
							pthread_mutex_lock(&mutex);
							curTag=firstTag;
							parentTag = NULL;
							while(curTag != NULL){
								if ( strcmp(tag, curTag->name)==0 ){
									if (FD_ISSET(fd, &curTag->clients)){
										curTag->numClients = 0;
										FD_CLR(fd, &curTag->clients);
									}
								//delete empty tags 
									if ( curTag->numClients == 0 ){
										if (parentTag == NULL){
											firstTag = curTag->link;
										} else {
											parentTag->link = curTag->link;
										}
										free(curTag->name);
										free(curTag);
									}
									break;
								} else if ( strcmp(tag, curTag->name) < 0 ) {
									curTag=curTag->link;
								} else {
									break;
								}
							}
							pthread_mutex_unlock(&mutex);
						}
								////////////////////////////////////////////////REGISTERALL///////////////////////////////
					} else if ( strncmp(buf, "REGISTERALL", 11 ) == 0) {
	            //if a client wants to get all messages, add it to a separate fdset regAll
						deregAllTags(fd);
						FD_SET(fd, &regAll);		

								//////////////////////////////////////////////DEREGISTERALL////////////////////////////
					} else if ( strncmp ( buf, "DEREGISTERALL", 13 ) == 0){
				//if a client wants to deregister from all msgs, remove it from fdsets of registered tags and regAll fdset  
						deregAllTags(fd);
						FD_CLR(fd, &regAll);

					} 

				}
			}
		}
	}
	return 0;
}


