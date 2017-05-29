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
#include <math.h>
#define BUFSIZE		4096

int connectsock( char *host, char *service, char *protocol );


int		csock;
char key[100]; 

void ksa(unsigned char state[], unsigned char key[], int len){
	int i,j=0,t; 

	for (i=0; i < 256; ++i)
		state[i] = i; 
	for (i=0; i < 256; ++i) {
		j = (j + state[i] + key[i % len]) % 256; 
		t = state[i]; 
		state[i] = state[j]; 
		state[j] = t; 
	}   
}

// Pseudo-Random Generator Algorithm 
// Input: state - the state used to generate the keystream 
//        out - Must be of at least "len" length
//        len - number of bytes to generate 
void prga(unsigned char state[], unsigned char out[], int len){  
	int i=0,j=0,x,t; 
	unsigned char key; 

	for (x=0; x < len; ++x)  {
		i = (i + 1) % 256; 
		j = (j + state[i]) % 256; 
		t = state[i]; 
		state[i] = state[j]; 
		state[j] = t; 
		out[x] = state[(state[i] + state[j]) % 256];
	}   
}

char* reverse(char *str){
	int i = 0;
	int j = strlen(str) - 1;
	char temp;
	while (i < j) {
		temp = str[i];
		str[i] = str[j];
		str[j] = temp;
		i++;
		j--;
	}
	return str;
}

char* crypt(char *buf, int size, int offset){
	int i, j;
	char state[256], stream[size]; 
	ksa( state, key, strlen(key) ); 
	prga( state, stream, size );

	for ( i=offset, j=0; i <size+offset; i++, j++ ){
		buf[i]=stream[j]^buf[i];
	} 
	return buf;
}	

int encrypt(char *buf, int* size){
	char msgBytes[2048];
	char tag [2048]="";
	int i, j;
	sscanf(buf, "%*s #%s", tag);			
	int taglen = strlen(tag);
	int start = 5 + ( (taglen==0)?0:(taglen+2) );
	for (i=start, j=0; i<*size; i++, j++){
		msgBytes[j]=buf[i];
	}
	int bytecount = *size-start-1;
	char number[5];
	sprintf(number, "%i", bytecount);
	int numLen=strlen(number);
	
	for (i=start, j=0; i<start+numLen; i++, j++){
		buf[i]=number[j];
	}
	buf[i]='/';
	i++;
	crypt(msgBytes, bytecount, 0);
	for (i, j=0; j<bytecount; i++, j++){
		buf[i]=msgBytes[j];
	}
	*size += numLen+1;
	return 1;
}

int decrypt(char* buf){
	char tag[2048]="";
	int i, bytecount, j;
	int taglen;
	if (sscanf(buf, "%*s #%s %i/", tag, &bytecount)==2){
		taglen = strlen(tag)+2;
	} else if (sscanf(buf, "%*s %i/", &bytecount)==1){
		taglen=0;
	} else {
		printf("ERROR READING MSG\n");
	}

	int numLen=log10(bytecount)	+ 1;
	int start = 6 + taglen +numLen;	
	read(csock, buf+start, bytecount+1);
	buf[start+bytecount+1]='\0';
	crypt(buf, bytecount, start);
	return start+bytecount;
}



void* listener(void* ign){
	printf("Started to listen\n");
	fflush(stdout);
	char buf[BUFSIZE];
	int cc, size, i=4, bytecount;
	for (;;){

		if ( (cc = read(csock, buf, 4)) <= 0){
			printf("\nconnection end\n");
			pthread_exit(NULL);
		}
		char command[5], ch;
		buf[4] = '\0';
		if (sscanf(buf, "%s ", command) < 1){
			printf("error in sscanf");
			fflush(stdout);
		}
		command[4]='\0';
		if (strcmp(command, "MSGE")==0){
			i = 4;
			while (1 ){
				if ((cc = read(csock, buf+i, 1)) <= 0){				  
					printf("\nconnection end\n");
					pthread_exit(NULL);	  
				}
				if (buf[i] == '/'){
					break;
				}
				i++;
			}
			buf[i+1] = '\0';
			size=decrypt(buf);
		} else if (strcmp(command, "MSG")==0){
			i = 4;
			while (1){
				if ( (cc = read(csock, buf+i, 1)) <= 0){				  
					printf("\nconnection end\n");
					pthread_exit(NULL);	  
				}

				if (buf[i] == '\n' && buf[i-1]=='\r'){
					break;
				}
				i++;
			}
			size=i;
		}
		buf[size] = '\0';
		printf("%s\n", buf);
		fflush(stdout);                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 
	}
	pthread_exit(NULL);
}

/*
**	Client
*/
int main( int argc, char *argv[] ) {
	char		buf[BUFSIZE];
	char		*service;
	char		*host = "localhost";
	int		cc;

	switch( argc ){
		case    2:
		service = argv[1];
		break;
		case    3:
		host = argv[1];
		service = argv[2];
		break;
		default:
		fprintf( stderr, "usage: chat [host] port\n" );
		exit(-1);
	}

	/*	Create the socket to the controller  */
	if ( ( csock = connectsock( host, service, "tcp" )) == 0 )
	{
		fprintf( stderr, "Cannot connect to server.\n" );
		exit( -1 );
	}
	printf("Please enter key (max 100 symbols)\n");
	fgets(key, 100, stdin);
	key[strlen(key)-1]='\0';
	pthread_t lstn;
	pthread_create( &lstn, NULL, listener, NULL );


	printf( "The server is ready, please start sending to the server.\n" );
	printf( "Type q or Q to quit.\n" );
	fflush( stdout );

	// 	Start the loop
	while ( fgets( buf, BUFSIZE, stdin ) != NULL ) {
		// If user types 'q' or 'Q', end the connection
		if ( buf[0] == 'q' || buf[0] == 'Q' ) {
			break;
		}
		int size=strlen(buf);
		char command[15];
		sscanf(buf, "%s ", command);
		if ( strcmp(command, "MSGE" )==0){
			if (!encrypt(buf, &size)){
				printf("Error encrypting file");
			}
		} else if (strcmp(command, "MSG")==0 || strcmp(command, "REGISTER") == 0 || strcmp(command, "DEREGISTER") == 0 || strcmp(command, "REGISTERALL") == 0 || strcmp(command, "DEREGISTERALL") == 0){
			buf[size]='\r';
			buf[size+1]='\n';
			size+=2;
		} 
		// Send to the server
		if ( (cc = write( csock, buf, size )) < 0 ) {
			fprintf( stderr, "client write: %s\n", strerror(errno) );
			exit( -1 );
		}
	}

	close( csock );

}
