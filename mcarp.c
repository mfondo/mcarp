/*
   mcarp is a somewhat crappy clone of ucarp
http://www.ucarp.org/project/ucarp

leaderScript.sh is a shell script to run when leadership changes
this script can do anything, but should do - flush arp cache, set up virtual ip address
argument 0 will be passed to tell leaderScript.sh that this node is not the leader, 1 is passed when it is the leader

Example args node1: ./mcarp -i 127.0.0.1:8888 -h 5000 -t 15000 -p 8889 -s /bin/leaderScript.sh
Example args node2: ./mcarp -i 127.0.0.1:8889 -h 5000 -t 15000 -p 8888 -s /bin/leaderScript.sh

TODO if isLeader and receive heartbeat do conflict resolution by assuming > ip string is leader,
so > ip would send a heartbeat immediately and the "false" leader would know it is not the real leader
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <sys/time.h>
#include <string.h>

#define MAX_HOSTS 10
#define BUFFER_LENGTH 1024
#define DEBUG

extern char * optarg;

int sendHeartBeatMillis;
int heartBeatTimeoutMillis;
struct sockaddr_in *addrs;
int addrsLength;
int serverPort;
struct sockaddr_in from;
int serverSock;
unsigned int fromlen = sizeof(struct sockaddr_in);
char buffer[BUFFER_LENGTH];
char heartBeatMsg[] = "heartbeat";
int heartBeatMsgLength;
char *leaderCommand;
char *leaderCommandIsLeader;
char *leaderCommandNotLeader;

inline long getTimeMillis() {
	return ((long)time(NULL)) * (long)1000;
}

void leaderChanged(int isLeader) {
	if(isLeader) {
		printf("IsLeader\n");
	} else {
		printf("NotLeader\n");
	}
	char *leaderCommandWithStatus;
	if(isLeader) {
		leaderCommandWithStatus = leaderCommandIsLeader;
	} else {
		leaderCommandWithStatus = leaderCommandNotLeader;
	}
	int result = system(leaderCommandWithStatus);
	if(result != -1) {
		result = WEXITSTATUS(result);
	}
	if(result != 0) {
		fprintf(stderr, "Leader command %s failed with exit code %i\n", leaderCommandWithStatus, result);
		//we could cancel considering self leader, but who knows what state the leaderCommand has become
	}	
}

void parseArgs(int argc, char *argv[]) {
	int parsedAddrs = 0;
	int parsedPort = 0;
	int opt;
	int leaderCommandLength;
	//http://linux.die.net/man/3/getopt
	while ((opt = getopt(argc, argv, "i:h:t:p:s:")) != -1) {
		switch (opt) {
			case 'i':
				parsedAddrs = 1;
				char *outerSavePtr;
				char *tmp = strtok_r(optarg, ",", &outerSavePtr);
				addrsLength = 0;
				while (tmp != NULL)	{
					if(addrsLength >= MAX_HOSTS) {
						fprintf(stderr, "Too many hosts\n");
						exit(1);
					}
					struct sockaddr_in *addr = &addrs[addrsLength];
					char *innerSavePtr;
					char *ipStr;
					char *portStr;
					char *tmp2 = strtok_r(tmp, ":", &innerSavePtr);
					int i = 0;
					while(tmp2 != NULL) {
						i++;
						if(i == 1) {
							ipStr = tmp2;
						} else if(i == 2) {
							portStr = tmp2;
						} else {
							printf("Invalid host:port %s\n", tmp);
							exit(1);
						}
						tmp2 = strtok_r(NULL, ":", &innerSavePtr); 
					}
					if(ipStr == NULL || portStr == NULL) {
						printf("Invalid host:port %s\n", tmp);
						exit(1);
					}
					int res = inet_pton(AF_INET, ipStr, &(addr->sin_addr));
					if(res < 0) {
						perror("Unknown error parsing ip");
					} else if(res == 0) {
						fprintf(stderr, "Invalid IP address\n");
						exit(1);
					}
					addr->sin_family = AF_INET;
					addr->sin_port = htons(atoi(portStr));
					tmp = strtok_r(NULL, ",", &outerSavePtr);	
					addrsLength++;
				}
				break;
			case 'h':
				sendHeartBeatMillis = atol(optarg);
				break;
			case 't':
				heartBeatTimeoutMillis = atol(optarg);
				break;
			case 'p':
				serverPort = atoi(optarg);
				parsedPort = 1;
				break;
			case 's':
				leaderCommandLength = strlen(optarg);
				if(leaderCommandLength < 1 || leaderCommandLength > 1000) {
					fprintf(stderr, "Invalid leader command\n");
					exit(1);
				}
				leaderCommand = (char *)malloc(leaderCommandLength * sizeof(char));
				strcpy(leaderCommand, optarg);
				int leaderCommandWithStatusLength = (strlen(leaderCommand) + 2) * sizeof(char);
				leaderCommandIsLeader = (char *)malloc(leaderCommandWithStatusLength);
				leaderCommandNotLeader = (char *)malloc(leaderCommandWithStatusLength);
				strcpy(leaderCommandIsLeader, leaderCommand); 
				strcpy(leaderCommandNotLeader, leaderCommand);
				strcat(leaderCommandIsLeader, " 1");
				strcat(leaderCommandNotLeader, " 0");
				break;
			default: /* '?' */
				fprintf(stderr, "Usage: %s [-t nsecs] [-n] name\n", argv[0]);
				exit(1);
		}
	}
	if(!parsedAddrs) {
		fprintf(stderr, "No ip provided\n");
		exit(1);
	}
	if(!parsedPort) {
		fprintf(stderr, "No port provided\n");
		exit(1);
	}
	if(!leaderCommand) {
		fprintf(stderr, "No leader command provided\n");
		exit(1);
	}
}

//return true if received a valid heartbeat message
int recvHeartbeat() {
	int numBytesReceived = recvfrom(serverSock, buffer, BUFFER_LENGTH, 0, (struct sockaddr *)&from, &fromlen);
	if(numBytesReceived < 0) {
		perror("Error receiving bytes");
		return 0;
	} else if(strncmp(heartBeatMsg, buffer, heartBeatMsgLength) == 0) {
		return 1;
	} else {
		return 0;
	}
}

int main(int argc, char *argv[]) {	
	leaderCommand = NULL;
	sendHeartBeatMillis = 5000;//send a heartbeat after this many millis
	heartBeatTimeoutMillis = sendHeartBeatMillis * 3;//assume leader is dead if no heartbeat after this long
	heartBeatMsgLength = strlen(heartBeatMsg);
	addrs = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in) * MAX_HOSTS);//using a list here would be more memory efficient
	if(addrs == NULL) {
		fprintf(stderr, "Could not allocate memory\n");
		exit(1);
	}
	parseArgs(argc, argv);

	serverSock = socket(AF_INET, SOCK_DGRAM, 0);
	if (serverSock < 0) {
		fprintf(stderr, "Opening socket\n");
		exit(1);
	}
	struct sockaddr_in serverSockAddr;
	memset(&serverSockAddr, 0, sizeof(serverSockAddr));
	serverSockAddr.sin_family = AF_INET;
	serverSockAddr.sin_addr.s_addr = INADDR_ANY;
	serverSockAddr.sin_port = htons(serverPort);
	if (bind(serverSock, (struct sockaddr *)&serverSockAddr, sizeof(struct sockaddr)) < 0)  {
		perror("Binding socket");
		exit(1);
	}

	struct pollfd fd;
	fd.fd = serverSock;
	fd.events = POLLIN;

	int isLeader = 0;
	leaderChanged(0);//initialize not leader

	while(1) {
		if(isLeader) {
			//send heartbeats unless we receive a heartbeat from someone else, then assume not leader
			long lastHeartbeatSentMillis = 0;
			long pollTimeout;
			int pollResult;
			int sendResult;
			while(1) {			
				//http://linux.die.net/man/2/poll	
				pollTimeout = lastHeartbeatSentMillis + sendHeartBeatMillis - getTimeMillis();				
				if(pollTimeout <= 0) {
					pollTimeout = sendHeartBeatMillis;
				}
				pollResult = poll(&fd, 1, (int)pollTimeout);
				if(pollResult < 0) {
					perror("IsLeader poll error");
				} else if(pollResult == 0) {
					int i = 0;
					for(i; i < addrsLength; i++) {
						memcpy(&buffer, heartBeatMsg, heartBeatMsgLength);
						sendResult = sendto(serverSock, buffer, BUFFER_LENGTH, 0, (const struct sockaddr *)(&addrs[i]), sizeof(struct sockaddr_in));
						if (sendResult < 0) {
							perror("IsLeader send error");
						}
					}					
					lastHeartbeatSentMillis = getTimeMillis();
				} else {
					if(fd.revents & POLLIN) {
						if(recvHeartbeat()) {
							//if we got here, someone else has taken over as leader, so we set isLeader = false						
							isLeader = 0;
							leaderChanged(isLeader);
						}						
						break;
					} else if(fd.revents & POLLERR) {
						//last message errored, what to do?
					}
				}
			}
		} else {
			long lastHeartbeatReceivedMillis = 0;
			int pollResult;
			long pollTimeout;
			while(1) {
				//listen for heartbeats, if don't hear heartbeat within x sec, assume self is leader
				pollTimeout = lastHeartbeatReceivedMillis + heartBeatTimeoutMillis - getTimeMillis();
				if(pollTimeout <= 0) {
					pollTimeout = heartBeatTimeoutMillis;
				}
				pollResult = poll(&fd, 1, (int)pollTimeout);
				if(pollResult < 0) {
					perror("IsNotLeader poll error");
				} else if(pollResult == 0) {
					isLeader = 1;
					leaderChanged(isLeader);
					break;
					//no heartbeats received from leader, take over leadership
					//TODO random delay to prevent slave storm to take over leadership?
				} else {
					if(fd.revents & POLLIN) {
						if(recvHeartbeat()) {
							lastHeartbeatReceivedMillis = getTimeMillis();
						}
					}
				}		
			}		
		}
	}
	return 0;
}

