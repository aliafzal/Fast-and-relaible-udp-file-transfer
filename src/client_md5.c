/* UDP client in the internet domain */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>


#if defined(__APPLE__)
#  define COMMON_DIGEST_FOR_OPENSSL
#  include <CommonCrypto/CommonDigest.h>
#  define SHA1 CC_SHA1
#else
#  include <openssl/md5.h>
#endif
 
#define data_size 1400

struct packet{
	uint8_t type;
	int sequence_number;
	char data[data_size];
};

struct Init_PACKET{
	uint8_t type;
	u_int file_size;
	u_int chunk_size;
};

struct ack_packet{
	uint8_t type;
	uint8_t packet_tracker[20000];
};

void error(const char *);

char *str2md5(const char *str, int length) {
    int n;
    MD5_CTX c;
    unsigned char digest[16];
    char *out = (char*)malloc(33);

    MD5_Init(&c);

    while (length > 0) {
        if (length > 512) {
            MD5_Update(&c, str, 512);
        } else {
            MD5_Update(&c, str, length);
        }
        length -= 512;
        str += 512;
    }

    MD5_Final(digest, &c);

    for (n = 0; n < 16; ++n) {
        snprintf(&(out[n*2]), 16*2, "%02x", (unsigned int)digest[n]);
    }

    return out;
}

int main(int argc, char *argv[])
{
   // file initializationa
	FILE * pFile;
	FILE *f2;
	long lSize;

	char data[60000];
	size_t result;
	int chunks;
	int final_chunk;
	struct packet data_packet = {0};
	char * packet_tobe_sent;
	char buffer1[256];
	bool state_ch = 1;
	
	// control init
	struct Init_PACKET init_packet = {0};
	char * send_buffer;
	struct ack_packet *ack_packet1;
	struct ack_packet ack_packet2;

	// socket initializations
	int sock, n;
	unsigned int length;
	struct sockaddr_in server, from;
	struct hostent *hp;

	// soclet configuration
	/*if (argc != 3) { printf("Usage: server port\n");
	              exit(1);
	}*/

	sock= socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) error("socket");

	server.sin_family = AF_INET;
	hp = gethostbyname("127.0.0.1");
	if (hp==0) error("Unknown host");

	bcopy((char *)hp->h_addr, (char *)&server.sin_addr,hp->h_length);
	server.sin_port = htons(atoi("12345"));
	length=sizeof(struct sockaddr_in);

  //opening up a file 

	pFile = fopen ( "data65.bin" , "rb" );
	f2 = fopen ( "log_client.txt" , "w" );
	if (pFile==NULL) {fputs ("File error",stderr); exit (1);}

	// obtain file size:
	fseek (pFile , 0 , SEEK_END);
	lSize = ftell (pFile);
	rewind (pFile);
	printf("File Size: %ld\n", lSize);
	chunks = ceil(lSize/(float)data_size);
	// allocate memory to contain the whole file:
	char **buffer = (char**) malloc (chunks*sizeof(char*));
	int count = 0;

	/// reading from file
	for(count = 0; count < chunks - 1;count++)
	{
		buffer[count] = (char*) malloc (data_size*sizeof(char));
		result = fread (buffer[count],1,data_size,pFile);
	} 
	printf("No of shunks : %d\n", chunks);
	printf("Max file size : %ld\n", lSize);
	printf("Memory read : %d\n", (chunks - 1)*data_size);
	printf("Memory left to read : %ld\n", lSize - (chunks-1)*data_size);
	printf("count value  : %d\n", count);


	/// remaining memory chunk lSize - chunks*data_size
	final_chunk = lSize - (chunks-1)*data_size;

	buffer[count] = (char*) malloc (final_chunk*sizeof(char));
	result = fread (buffer[count],1,final_chunk,pFile);
	printf("File Size read: %d, count : %d\n", result,count);

	

	init_packet.type = 0;
	init_packet.file_size = lSize;
	init_packet.chunk_size = data_size;
	send_buffer = (unsigned char*)malloc(sizeof(struct Init_PACKET));
	memset(send_buffer,0,sizeof(struct Init_PACKET));
	memcpy(send_buffer,(const unsigned char*)&init_packet,sizeof(init_packet));
	n=sendto(sock,send_buffer,sizeof(init_packet),0,(const struct sockaddr *)&server,length);
	if (n < 0) error("Sendto");
	free(send_buffer);
	printf("Init packet sent \n");
	
	int send_counter = 0;

	struct timeval timeout={0,10000};
    setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,(char*)&timeout,sizeof(struct timeval));


	while(1)
	{
		n = recvfrom(sock,buffer1,sizeof(struct Init_PACKET),0,(struct sockaddr *)&from, &length);
		if(n > 0)
		{
			ack_packet1 = (struct ack_packet*)buffer1;
			if(ack_packet1->type == 4)
			{
			    printf("\n");
				printf("Ack of init received \n");
				break;
			}	
		}
		else
		{
			for (send_counter = 0; send_counter<1;send_counter++)
			{
				init_packet.type = 0;
				init_packet.file_size = lSize;
				init_packet.chunk_size = data_size;
				send_buffer = (unsigned char*)malloc(sizeof(struct Init_PACKET));
				memset(send_buffer,0,sizeof(struct Init_PACKET));
				memcpy(send_buffer,(const unsigned char*)&init_packet,sizeof(init_packet));
				n=sendto(sock,send_buffer,sizeof(init_packet),0,(const struct sockaddr *)&server,length);
				if (n < 0) error("Sendto");
				free(send_buffer);
				printf("Init packet no : %d\n",send_counter);
			}
		}
		
	}
	
	printf("Going for the data\n");
	packet_tobe_sent = (unsigned char*)malloc(sizeof(struct packet));
	printf("1\n");
	while(1)
	{
		if(state_ch == 1)
		{
			
			int send_count = 0;
			for(send_count = 0; send_count < chunks-1; send_count++)
			{
				if(ack_packet1->packet_tracker[send_count] == 0)
				{
					data_packet.type = 1;
					data_packet.sequence_number = send_count;
					memcpy(data_packet.data,buffer[send_count],data_size);
					memset(packet_tobe_sent,0,sizeof(struct packet));
					memcpy(packet_tobe_sent,(const unsigned char*)&data_packet,sizeof(data_packet));
				  	char *output = str2md5(data_packet.data, data_size);
				  	char *output1 = str2md5(buffer[send_count], data_size);
        			fprintf(f2,"type: %d, sequence number : %d hash : %s hash1: %s\n",data_packet.type,data_packet.sequence_number, output,output1);
					n=sendto(sock,packet_tobe_sent,data_size,0,(const struct sockaddr *)&server,length);
					if (n < 0) error("Sendto");
					printf("type: %d, sequence number : %d\n",data_packet.type,data_packet.sequence_number);
				}
			}
			state_ch = 0;
			
			data_packet.type = 2;
			data_packet.sequence_number = -1;
			memset(packet_tobe_sent,0,sizeof(struct packet));
			memcpy(packet_tobe_sent,(const unsigned char*)&data_packet,sizeof(data_packet));
			n=sendto(sock,packet_tobe_sent,sizeof(data_packet),0,(const struct sockaddr *)&server,length);
			
			if (n < 0) error("Sendto");
			printf("type: %d, sequence number : %d\n",data_packet.type,data_packet.sequence_number);
			
		}
		else
		{
			n = recvfrom(sock,buffer1,sizeof(struct ack_packet),0,(struct sockaddr *)&from, &length);
			ack_packet1 = (struct ack_packet*)buffer1;
			printf("type %d\n",ack_packet1->type);
			if(n > 0)
			{
				
				if(ack_packet1->type == 2)
				{
					state_ch = 1;
				}
				else if(ack_packet1->type == 3)
				{
					printf("all packets received except last\n");
					break;
				}
				
			}
			else
			{
				
				data_packet.type = 2;
				data_packet.sequence_number = -1;
				memset(packet_tobe_sent,0,sizeof(struct packet));
				memcpy(packet_tobe_sent,(const unsigned char*)&data_packet,sizeof(data_packet));
				n=sendto(sock,packet_tobe_sent,sizeof(data_packet),0,(const struct sockaddr *)&server,length);
			
				if (n < 0) error("Sendto");
				printf("type: %d, sequence number : %d\n",data_packet.type,data_packet.sequence_number);
			}
		}
		
	}

	printf("Here\n");
	while(1)
	{
		
		data_packet.type = 6;
		data_packet.sequence_number = chunks-1;
		memcpy(data_packet.data,buffer[chunks-1],data_size);
		memset(packet_tobe_sent,0,sizeof(struct packet));
		memcpy(packet_tobe_sent,(const unsigned char*)&data_packet,sizeof(data_packet));
	  
		n=sendto(sock,packet_tobe_sent,sizeof(data_packet),0,(const struct sockaddr *)&server,length);
		if (n < 0) error("Sendto");
		printf("type: %d, sequence number : %d , Bytes sent : %d\n",data_packet.type,data_packet.sequence_number,n);
		n = recvfrom(sock,buffer1,sizeof(struct ack_packet),0,(struct sockaddr *)&from, &length);
		if(n > 0)
		{
			printf("Packet 5 received : \n");
			struct ack_packet* ack_packet3 = (struct ack_packet*)buffer1;
			printf("type last %d\n",ack_packet3->type);
			if(ack_packet3->type == 5 || ack_packet3->type == 3)
			{
				printf("all packets sent \n");
				break;
			}

		}
	}
	close(sock);
	fclose (pFile);
	free (buffer);
	return 0;
}

void error(const char *msg)
{
    perror(msg);
    exit(0);
}
