#include <iostream>
#include <queue>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <net/if.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/errno.h>

#define THREAD_WORKERS 50

std::string ip ;
std::queue <pthread_t> Workers ;
sem_t workPool ;
sem_t mutex ;

size_t initWorkers(size_t ports) {
    size_t size = (THREAD_WORKERS > ports) ? ports : THREAD_WORKERS ;
    pthread_t workers[size] ;
    for (int i = 0; i < size; i++) {
        Workers.push(workers[i]) ;
    }
	return size ;
}

pthread_t getWorker() {
    pthread_t worker = Workers.front() ;
    Workers.pop() ;
    return worker ;
}

std::string getIp() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0) ;
    ifreq ifr ;
    ifr.ifr_addr.sa_family = AF_INET ;
    strncpy(ifr.ifr_name, "eth0", IFNAMSIZ - 1) ;
    ioctl(fd, SIOCGIFADDR, &ifr) ;
    close(fd) ;
    return std::string(inet_ntoa(((sockaddr_in *)&ifr.ifr_addr)->sin_addr)) ;
}

void * portOpen(void * arg) {
    sem_wait (&mutex) ;
    Workers.push(pthread_self()) ;
    size_t port = *(size_t *)arg ;

    sockaddr_in target ;
    bzero(&target, sizeof(target)) ;
    target.sin_family       = AF_INET ;
    target.sin_addr.s_addr  = inet_addr(ip.c_str()) ;
    target.sin_port         = htons(port) ;

    int sockfd ;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) != -1) {
        fcntl (sockfd, F_SETFL, O_NONBLOCK) ;

        int cnnctVal = connect(sockfd, (struct sockaddr*)&target, sizeof(target)) ;
        if (cnnctVal == 0 || (cnnctVal == -1 && errno == EINPROGRESS)) {
            fd_set fds ;    
            FD_ZERO(&fds) ;
            FD_SET(sockfd, &fds) ;
            timeval timeOut = {1, 0} ;

            if (select(sockfd + 1, NULL, &fds, NULL, &timeOut) == 1) {
                int error ;
                socklen_t len = sizeof(error) ;
                getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) ;
                if (error == 0) {
                    char prt[5] ;
                    sprintf(prt, "%lu", port) ;

                    addrinfo hints ;
                    memset(&hints, 0, sizeof(hints)) ;
                    hints.ai_family     = AF_UNSPEC ; 
                    hints.ai_socktype   = SOCK_STREAM ;                    
                    hints.ai_flags      = AI_PASSIVE ;

                    addrinfo *ai ;
                    if (getaddrinfo(NULL, prt, &hints, &ai) != 0) {
                        char servName[50] ;                                            
                        getnameinfo(ai->ai_addr, ai->ai_addrlen, NULL, 0, servName, sizeof(servName), 0) ;
                        std::cout << ip << ":" << port << " " << servName << std::endl ;
                    }
                    freeaddrinfo(ai) ;
                }
            }
        }
    }

    close(sockfd) ;
    sem_post (&mutex) ;
    sem_post (&workPool) ;
    return NULL ;
}


int main(int argc, char* argv[]) {
    int portLow = 0 ;
    int portHig = 65535 ;
    if (argc == 1) {
        ip = getIp() ;
    } else {
        if (std::string(argv[1]) == "localhost" || std::string(argv[1]) == "127.0.0.1") {
            ip = getIp() ;
        } else if (inet_pton(AF_INET, argv[1], NULL) != 0) {
            ip = inet_addr(argv[1]) ;
        } else if (isalpha(argv[1][0])){
            struct hostent* host ;
            if ((host = gethostbyname(argv[1])) != 0) {
                struct in_addr** addr ;
                addr = (struct in_addr **) host->h_addr_list ;
                ip = inet_ntoa(*addr[0]) ;            
            } else {
                std::cout << "Invalid Hostname!" << std::endl ;
                return 1 ;
            }
        } else {
            std::cout << "Invalid IP Address!" << std::endl ;
            return 1 ;
        }
        if (argc >= 3) {
            if ((portLow = atoi(argv[2])) < 0 || portLow > 65535) {
                std::cout << "Invalid starting port specified!" << std::endl ;
                return 1 ;
            }
        }
        if (argc >= 4) {
            if ((portHig = atoi(argv[3])) < 0 || portHig > 65535 || portHig < portLow) {
                std::cout << "Invalid ending port specified!" << std::endl ;
                return 1 ;
            }
        }
    }

    // Create Worker Pool
    size_t noPorts  = portHig - portLow + 1 ;
    size_t workers  = initWorkers(noPorts) ;   
    pthread_t threads[workers] ;
    sem_init(&workPool, 0, workers) ;
    sem_init(&mutex, 0, 1) ;
    size_t ports[noPorts] ;
    for (int i = 0; i < noPorts; i++) {
        ports[i] = portLow + i ;
    }

    std::cout << "Scanning IP = " << ip << ", ports " << portLow << " -> " << portHig << std::endl ;    
    for (int i = 0; i < noPorts; i++) {
        sem_wait (&workPool) ;    
        threads[i % workers] = getWorker() ;
        pthread_create (&threads[i % workers], NULL, portOpen, (void*) &ports[i]) ;
    }

    for (int i = 0; i < workers; i++) {
        pthread_join(threads[i], NULL) ;
    }

    sem_destroy(&workPool) ;
    pthread_exit(NULL) ;

    return 0 ;
}
