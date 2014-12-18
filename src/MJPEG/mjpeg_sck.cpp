#include "mjpeg_sck.hpp"

#include <algorithm>

#ifdef _WIN32
void
_sck_wsainit(){
    WORD vs;
    WSADATA wsadata;

    vs = MAKEWORD(2, 2);
    WSAStartup(vs, &wsadata);

    return;
}
#endif

int
mjpeg_sck_valid(mjpeg_socket_t sd)
{
#ifdef _WIN32
    if (sd == INVALID_SOCKET) {
        return 0;
    }
#else
    if (sd < 0) {
        return 0;
    }
#endif
    return 1;
}

int
mjpeg_sck_setnonblocking(mjpeg_socket_t sd, int enable)
{
    int error;
#ifdef _WIN32
    u_long nbenable = enable;
    error = ioctlsocket(sd, FIONBIO, &nbenable);
    if (error != 0) {
        mjpeg_sck_close(sd);
        return -1;
    }
#else
    int flags = fcntl(sd, F_GETFL, 0);
    if (flags == -1) {
        mjpeg_sck_close(sd);
        return -1;
    }
    if (enable != 0) {
        error = fcntl(sd, F_SETFL, flags | O_NONBLOCK);
    }
    else {
        error = fcntl(sd, F_SETFL, flags & ~O_NONBLOCK);
    }
    if(error == -1) {
        mjpeg_sck_close(sd);
        return -1;
    }
#endif
    return 0;
}

mjpeg_sck_status
mjpeg_sck_geterror()
{
#ifdef _WIN32
    switch (WSAGetLastError()) {
        case WSAEWOULDBLOCK :  return SCK_NOTREADY;
        case WSAECONNABORTED : return SCK_DISCONNECT;
        case WSAECONNRESET :   return SCK_DISCONNECT;
        case WSAETIMEDOUT :    return SCK_DISCONNECT;
        case WSAENETRESET :    return SCK_DISCONNECT;
        case WSAENOTCONN :     return SCK_DISCONNECT;
        default :              return SCK_ERROR;
    }
#else
    // The followings are sometimes equal to EWOULDBLOCK,
    // so we have to make a special case for them in order
    // to avoid having double values in the switch case
    if ((errno == EAGAIN) || (errno == EINPROGRESS)) {
        return SCK_NOTREADY;
    }

    switch (errno) {
        case EWOULDBLOCK :  return SCK_NOTREADY;
        case ECONNABORTED : return SCK_DISCONNECT;
        case ECONNRESET :   return SCK_DISCONNECT;
        case ETIMEDOUT :    return SCK_DISCONNECT;
        case ENETRESET :    return SCK_DISCONNECT;
        case ENOTCONN :     return SCK_DISCONNECT;
        case EPIPE :        return SCK_DISCONNECT;
        default :           return SCK_ERROR;
    }
#endif
}

/* mjpeg_sck_connect() attempts to connect to the specified
   remote host on the specified port. The function blocks
   until either cancelfd becomes ready for reading, or the
   connection succeeds or times out.
   If the connection succeeds, the new socket descriptor
   is returned. On error, -1 isreturned, and errno is
   set appropriately. */
int
mjpeg_sck_connect(const char *host, int port, int cancelfd)
{
    int sd;
    int error;
    int error_code;
    int error_code_len;

    struct hostent *hp;
    struct sockaddr_in pin;
    fd_set readfds;
    fd_set writefds;
    fd_set exceptfds;

    #ifdef _WIN32
    _sck_wsainit();
    #endif

    /* Create a new socket */
    sd = socket(AF_INET, SOCK_STREAM, 0);
    if(mjpeg_sck_valid(sd) == 0) return -1;

    /* Set the non-blocking flag */
    error = mjpeg_sck_setnonblocking(sd, 1);
    if ( error != 0){
        return error;
    }

    /* Resolve the specified hostname to an IPv4
       address. */
    hp = gethostbyname(host);
    if(hp == NULL) {
        mjpeg_sck_close(sd);
        return -1;
    }

    /* Set up the sockaddr_in structure. */
    memset(&pin, 0, sizeof(struct sockaddr_in));
    pin.sin_family = AF_INET;
    pin.sin_addr.s_addr = ((struct in_addr *)(hp->h_addr_list[0]))->s_addr;
    pin.sin_port = htons(port);

    /* Try to connect */
    error = connect(
        sd,
        (struct sockaddr *)&pin,
        sizeof(struct sockaddr_in));

#ifdef _WIN32
    if(error != 0 && WSAGetLastError() != WSAEWOULDBLOCK) {
        mjpeg_sck_close(sd);
        return -1;
    }
#else
    if(error != 0 && errno != EINPROGRESS) {
        mjpeg_sck_close(sd);
        return -1;
    }
#endif

    /* select(2) for reading and exceptions on the socket and
       cancelfd. */
    FD_ZERO(&readfds);
    FD_SET(cancelfd, &readfds);

    FD_ZERO(&writefds);
    FD_SET(sd, &writefds);

    FD_ZERO(&exceptfds);
    FD_SET(sd, &exceptfds);
    FD_SET(cancelfd, &exceptfds);
    error = select(std::max(sd, cancelfd)+1, &readfds, &writefds, &exceptfds, NULL);
    if(error == -1) {
        mjpeg_sck_close(sd);
        return -1;
    }

    /* We were interrupted by data at cancelfd before we could
       finish connecting. */
    if(FD_ISSET(cancelfd, &readfds)) {
        mjpeg_sck_close(sd);
#ifdef _WIN32
        WSASetLastError(WSAETIMEDOUT);
#else
        errno = ETIMEDOUT;
#endif
        return -1;
    }

    /* Something bad happened. Probably one of the sockets
       selected for an exception. */
    if(!FD_ISSET(sd, &writefds)) {
        mjpeg_sck_close(sd);
#ifdef _WIN32
        WSASetLastError(WSAEBADF);
#else
        errno = EBADF;
#endif
        return -1;
    }

    /* Check that connecting was successful. */
    error_code_len = sizeof(error_code);
    error = getsockopt(sd, SOL_SOCKET, SO_ERROR, (char *) &error_code, (socklen_t *) &error_code_len);
    if(error == -1) {
      mjpeg_sck_close(sd);
      return -1;
    }
    if(error_code != 0) {
    /* Note: Setting errno on systems which either do not support
       it, or whose socket error codes are not consistent with its
       system error codes is a bad idea. */
#if _WIN32
        WSASetLastError(error);
#else
        errno = error;
#endif
        return -1;
    }

    /* We're connected */
    return sd;
}

int
mjpeg_sck_close(mjpeg_socket_t sd)
{
#ifdef _WIN32
    return closesocket(sd);
#else
    return close(sd);
#endif
}
