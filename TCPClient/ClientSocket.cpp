#include "ClientSocket.h"

int ClientSocket::initWinSock()
{
    int iResult;

    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup failed with error: %d\n", iResult);
        return 1;
    }

	return 0;
}

int ClientSocket::initTCPClient()
{
    struct addrinfo *result = NULL,
                    *ptr = NULL,
                    hints;

    int iResult;

    ZeroMemory( &hints, sizeof(hints) );
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

	char portStr[5];
	sprintf(portStr, "%d", _tcpAudioServerPort);

    // Resolve the server address and port
    iResult = getaddrinfo(_tcpAudioServerIP, portStr, &hints, &result);
    if ( iResult != 0 ) {
        printf("getaddrinfo failed with error: %d\n", iResult);
        WSACleanup();
        return 1;
    }

    // Attempt to connect to an address until one succeeds
    for(ptr=result; ptr != NULL ;ptr=ptr->ai_next) {

        // Create a SOCKET for connecting to server
        ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (ConnectSocket == INVALID_SOCKET) {
            printf("socket failed with error: %ld\n", WSAGetLastError());
            WSACleanup();
            return 1;
        }

        // Connect to server.
        iResult = connect( ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
        if (iResult == SOCKET_ERROR) {
            closesocket(ConnectSocket);
            ConnectSocket = INVALID_SOCKET;
            continue;
        }
        break;
    }

    freeaddrinfo(result);

    if (ConnectSocket == INVALID_SOCKET) {
        printf("Unable to connect to server!\n");
        WSACleanup();
        return 1;
    }

	return 0;
}

int ClientSocket::initUDPClient()
{
    struct addrinfo *result = NULL,
                    *ptr = NULL,
                    hints;
    
    int iResult;

    ZeroMemory( &hints, sizeof(hints) );
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = 0;

	char portStr[5];
	sprintf(portStr, "%d", _udpAudioServerPort);

    // Resolve the server address and port
	iResult = getaddrinfo(_udpAudioServerIP, portStr, &hints, &result);
    if ( iResult != 0 ) {
        printf("getaddrinfo failed with error: %d\n", iResult);
        WSACleanup();
        return 1;
    }

    // Attempt to connect to an address until one succeeds
    for(ptr=result; ptr != NULL ;ptr=ptr->ai_next) {

        // Create a SOCKET for connecting to server
        ConnectSocketUDP = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (ConnectSocketUDP == INVALID_SOCKET) {
            printf("socket failed with error: %ld\n", WSAGetLastError());
            WSACleanup();
            return 1;
        }

        // Connect to server.
        iResult = connect( ConnectSocketUDP, ptr->ai_addr, (int)ptr->ai_addrlen);
        if (iResult == SOCKET_ERROR) {
            closesocket(ConnectSocketUDP);
            ConnectSocket = INVALID_SOCKET;
            continue;
        }
		printf("Connected!\n");
        break;
    }

    freeaddrinfo(result);

    if (ConnectSocketUDP == INVALID_SOCKET) {
        printf("Unable to connect to server!\n");
        WSACleanup();
        return 1;
    }

	//// Set non-blocking
	//unsigned long l = 1;
	//DWORD Returned = 0;
	//if ( SOCKET_ERROR == WSAIoctl ( ConnectSocketUDP, FIONBIO, (LPVOID)&l, sizeof(l), NULL, 0, &Returned, NULL, NULL ) )
	//{
	//	printf("Failed to set non-blocking mode, error %d\n", WSAGetLastError() );
	//}
	//WSAEWOULDBLOCK

	return 0;
}

ClientSocket::ClientSocket()
{
	ConnectSocket = INVALID_SOCKET;
	ConnectSocketUDP = INVALID_SOCKET;

	initWinSock();
}

/************************************* TCP *************************************/

int ClientSocket::sendTCPData(const char* msg)
{
	int iResult;
	//char sendbuf[DEFAULT_BUFLEN];
	//sendbuf = "CLIENTID;4;";
	//memset(sendbuf, 0, DEFAULT_BUFLEN);
	//sendbuf[0] = STR_END_MSG;
	//strcpy(sendbuf, "CLIENTID;4;");
	char *sendbuf = (char*)msg;//"this is a test";
	//sendbuf[12] = (char)"";//(char)0; //for flash
	    // Send an initial buffer
    iResult = send( ConnectSocket, sendbuf, DEFAULT_BUFLEN, 0 );
    if (iResult == SOCKET_ERROR) {
        printf("send failed with error: %d\n", WSAGetLastError());
        closesocket(ConnectSocket);
        WSACleanup();
        return 1;
    }

    printf("Bytes Sent:%ld\n", iResult);
	printf("Bytes Sent:%s\n", sendbuf);

    //// shutdown the connection since no more data will be sent
    //iResult = shutdown(ConnectSocket, SD_SEND);
    //if (iResult == SOCKET_ERROR) {
    //    printf("shutdown failed with error: %d\n", WSAGetLastError());
    //    closesocket(ConnectSocket);
    //    WSACleanup();
    //    return 1;
    //}

	return 0;
}

int ClientSocket::getTCPData(char* msg)
{
    char recvbuf[DEFAULT_BUFLEN];
    int iResult = -1;
    int recvbuflen = DEFAULT_BUFLEN;

    // Receive until the peer closes the connection
    //do {

        iResult = recv(ConnectSocket, recvbuf, recvbuflen, 0);
        if ( iResult > 0 )
		{
            //printf("Bytes received: %d\n", iResult);
			//recvbuf[iResult] = '\0';
			sprintf(msg, "%s", recvbuf);
			//msg = "Hello";//recvbuf;

			//if( stricmp(recvbuf, "READY;") == 0 )
			//	break;
		}
        else if ( iResult == 0 )
            printf("Connection closed\n");
        else
            printf("recv failed with error: %d\n", WSAGetLastError());

    //} while( iResult > 0 );

	return iResult;
}



/************************************* UDP *************************************/

int ClientSocket::sendUDPData(const char* msg)
{
	int iResult;
	//memset(sendbuf, 0, DEFAULT_BUFLEN);
	char *sendbuf = (char*)msg;//"this is a test";
	//sendbuf[12] = (char)"";//(char)0; //for flash
	// Send an initial buffer
		//(int)strlen(sendbuf)
    iResult = send( ConnectSocketUDP, sendbuf, DEFAULT_BUFLEN, 0 );
    if (iResult == SOCKET_ERROR) {
        printf("send failed with error: %d\n", WSAGetLastError());
        closesocket(ConnectSocketUDP);
        WSACleanup();
        return 1;
    }

    printf("Bytes Sent:%ld\n", iResult);
	printf("Bytes Sent:%s\n", sendbuf);

    //// shutdown the connection since no more data will be sent
    //iResult = shutdown(ConnectSocketUDP, SD_SEND);
    //if (iResult == SOCKET_ERROR) {
    //    printf("shutdown failed with error: %d\n", WSAGetLastError());
    //    closesocket(ConnectSocketUDP);
    //    WSACleanup();
    //    return 1;
    //}

	return 0;
}

int ClientSocket::getUDPData(char* msg)
{
    char recvbuf[DEFAULT_BUFLEN];
    int iResult = -1;
    int recvbuflen = DEFAULT_BUFLEN;

    // Receive until the peer closes the connection
    //do {

        iResult = recv(ConnectSocketUDP, recvbuf, recvbuflen, 0);
        if ( iResult > 0 )
		{
            printf("Bytes received: %d\n", iResult);
			recvbuf[iResult] = '\0';
			sprintf(msg, "%s", recvbuf);
			//msg = "Hello";//recvbuf;

			//if( stricmp(recvbuf, "READY;") == 0 )
			//	break;
		}
        else if ( iResult == 0 )
            printf("Connection closed\n");
        else
            printf("recv failed with error: %d\n", WSAGetLastError());

    //} while( iResult > 0 );

	return iResult;
}

