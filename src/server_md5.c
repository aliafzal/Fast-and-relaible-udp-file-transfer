#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <netdb.h>
#include <stdio.h>
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


void error(const char *msg)
{
    perror(msg);
    exit(0);
}

struct Init_PACKET{
	uint8_t type;
	u_int file_size;
	u_int chunk_size;
};

struct packet{
	uint8_t type;
	int sequence_number;
	unsigned char data[data_size];
};

struct ack_packet{
	uint8_t type;
	char packet_tracker[20000];
};


int main(int argc, char *argv[])
{
	FILE * oFile;
	FILE * f1;
	int sock, length, n;
	socklen_t fromlen;
	struct sockaddr_in server;
	struct sockaddr_in from;
	char buf[63000];
	char send_buffer[10000];
	int chunks, final_chunk;
	bool init_check = 0, mem_alloc_ch=0, data_check=0;
	int cons_check = 0;
	int type; 
	struct ack_packet ack_packet1;
	int packet_counter = 0; 

	f1 = fopen ( "log_server.txt" , "w" );
	
	/*if (argc < 2) 
	{
		fprintf(stderr, "ERROR, no port provided\n");
		exit(0);
	}*/
	sock=socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) error("Opening socket");
	length = sizeof(server);
	bzero(&server,length);
	server.sin_family=AF_INET;
	server.sin_addr.s_addr=INADDR_ANY;
	server.sin_port=htons(atoi("12345"));
	if (bind(sock,(struct sockaddr *)&server,length)<0) 
		error("binding");

	fromlen = sizeof(struct sockaddr_in);

	int file_sz;
	int send_counter = 0;
	printf("Waiting for init packet \n");
	while(1)
	{
		n = recvfrom(sock,buf,sizeof(struct Init_PACKET),0,(struct sockaddr *)&from,&fromlen);
		if (n < 0) error("recvfrom");
		
		printf("No of bytes received: %d\n",n);
		struct Init_PACKET* recv_init_packet = (struct Init_PACKET*)buf;
		if(recv_init_packet->type == 0)
		{
			init_check = 1;
			printf("Init packet received decoding ... \n");
			file_sz = recv_init_packet->file_size;
			printf("File size : %d\n",file_sz);
			printf("Data size : %d\n",recv_init_packet->chunk_size);
			break;
		}
	}
	
	printf("allocating memory\n");
	chunks = ceil(file_sz/(float)data_size);
	char **buffer = (char**) malloc (chunks*sizeof(char*));
	int count = 0;
	for(count = 0; count < chunks - 1;count++)
	{
		buffer[count] = (char*) malloc (data_size*sizeof(char));
	} 
	final_chunk = file_sz - (chunks-1)*data_size;
	buffer[count] = (char*) malloc (final_chunk*sizeof(char));
	
	
	ack_packet1.type = 4;
	int b=0;
	printf("Type packet1 %d\n",ack_packet1.type);
    

	memcpy(send_buffer,(unsigned char*)&ack_packet1,sizeof(ack_packet1));
	n = sendto(sock,send_buffer,sizeof(ack_packet1),0,(struct sockaddr *)&from,fromlen);
	if (n  < 0) error("sendto");
	mem_alloc_ch = 1;
	printf("allocating memory done \n");
	printf("Now waiting for data\n");
	while (1) 
	{
		n = recvfrom(sock,buf,data_size,0,(struct sockaddr *)&from,&fromlen);
		
		struct packet* data_packet = (struct packet*)buf;
		
		type = data_packet->type;
		if(type == 0)
		{
			ack_packet1.type = 4;
			memcpy(send_buffer,(unsigned char*)&ack_packet1,sizeof(ack_packet1));
			n = sendto(sock,send_buffer,sizeof(ack_packet1),0,(struct sockaddr *)&from,fromlen);
			if (n  < 0) error("sendto");
		}
		else if(type == 1)
		{

			struct packet* data_packet = (struct packet*)buf;
			if(ack_packet1.packet_tracker[data_packet->sequence_number] <= 0)
			{
				printf("Received seq no : %d\n",data_packet->sequence_number);

				memcpy(buffer[data_packet->sequence_number],data_packet->data,data_size);
				char *output = str2md5(buffer[data_packet->sequence_number], data_size);
			  	char *output1 = str2md5(data_packet->data, data_size);
    			fprintf(f1,"type: %d, sequence number : %d hash : %s hash1: %s\n",data_packet->type,data_packet->sequence_number, output,output1);
				ack_packet1.packet_tracker[data_packet->sequence_number] = 1;
				packet_counter++;
			}
		}
		else if(type == 2)
		{
			printf("Packet counter val %d\n",packet_counter);
			//fprintf(f1,"Packet counter val %d\n",packet_counter);
			
			int b;
			
			fprintf(f1,"\n");
			if(packet_counter >= (chunks-2))
			{
				printf("All packets received except last \n");
				ack_packet1.type = 3;
				memcpy(send_buffer,(unsigned char*)&ack_packet1,sizeof(ack_packet1));
				n = sendto(sock,send_buffer,sizeof(ack_packet1),0,(struct sockaddr *)&from,fromlen);
				if (n  < 0) error("sendto");
				fprintf(f1, "p: ");
				for(b = 0; b<chunks;b++)
				{
					fprintf(f1,"%d",ack_packet1.packet_tracker[b]);

				}
				fprintf(f1,"\n");
				break;
			}
			else
			{
				
				ack_packet1.type = 2;
				memcpy(send_buffer,(unsigned char*)&ack_packet1,sizeof(ack_packet1));
				n = sendto(sock,send_buffer,sizeof(ack_packet1),0,(struct sockaddr *)&from,fromlen);
				if (n  < 0) error("sendto");

			}

		}
	}
	while(1)
	{
		printf("Waiting for last packet\n");
		n = recvfrom(sock,buf,final_chunk,0,(struct sockaddr *)&from,&fromlen);
		printf("No of bytes received %d\n",n );
		struct packet* data_packet = (struct packet*)buf;
		printf("type %d : Received seq no : %d \n",data_packet->type,data_packet->sequence_number);
		
		printf("1\n");
		if(data_packet->type == 6)
		{
			memcpy(buffer[data_packet->sequence_number],data_packet->data,final_chunk-1);
			printf("2\n");
			int s_count =0;
			
			for(s_count = 0;s_count<10;s_count++)
			{
				printf("Sending type5\n");
				ack_packet1.type = 5;
				memcpy(send_buffer,(unsigned char*)&ack_packet1,sizeof(ack_packet1));
				n = sendto(sock,send_buffer,sizeof(ack_packet1),0,(struct sockaddr *)&from,fromlen);
				if (n  < 0) error("sendto");
			}
			break;
		}
		else if(data_packet->type == 2)
		{
			ack_packet1.type = 3;
			memcpy(send_buffer,(unsigned char*)&ack_packet1,sizeof(ack_packet1));
			n = sendto(sock,send_buffer,sizeof(ack_packet1),0,(struct sockaddr *)&from,fromlen);
			if (n  < 0) error("sendto");
		}
		
	}
  
  oFile = fopen ( "received_test1.bin" , "wb" );
printf("3\n");
  int w_count = 0;
  for(w_count = 0; w_count<chunks-1;w_count++)
  {
    fwrite (buffer[w_count] , sizeof(char), data_size, oFile);
  }
  printf("4\n");
  fwrite (buffer[w_count], sizeof(char), final_chunk, oFile);
  printf("5\n");

  fclose (oFile);
  close(sock);
  
  return 0;
}
