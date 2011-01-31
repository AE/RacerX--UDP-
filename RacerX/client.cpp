/* * * * *Current Issues* * * */
/*
/*  QImage will be lost
/*
/*
* * * * * * * * * * * * * * * */

#include "client.h"
#include <opencv/cv.h>
#include <iostream>

// see mainwindow.cpp
extern pthread_mutex_t mutex_s;
extern pthread_mutex_t mutex_c;
extern Data clientData;
extern int sock, sockfd;

extern char serverIP[15];
extern int commandPort;
extern int streamPort;

// variables used to implement sliding window
// protocol to receive image and telemetry
// data via udp
const int windowSize = 3; // stores the window size
int currWind = 0; // stores the starting index of the current window
int latestFrame = -1; // stores the index of the latest frame received
unsigned char readBuffer[windowSize][200000]; // buffer to store data received
int pktsReceived[windowSize][200]; // stores number of packets received for each frame

Client::Client()
{

}

int Client::connect(void)
{

    //Initialize stream and control threads.
    if(pthread_create(&thread_s, NULL, startStream, NULL))
    {
        return -1;
    }

    if(pthread_create(&thread_c, NULL, startControl, NULL))
    {

        return -1;
    }

    return 0;
}

int Client::disconnect(void)
{

        //Cancel stream and control threads.
        if(pthread_cancel(thread_s))
        {
            return -1;
        }

        if(pthread_cancel(thread_c))
        {
            return -1;
        }

        if(sock) ::close(sock);
        if(sockfd) ::close(sockfd);

        cvWaitKey(30);

        return 0;
}

void Client::streamThread(void)
{
    struct sockaddr_in connection;

    /* Make this thread cancellable using pthread_cancel(). */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    /* create socket */
    if ((sock = ::socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        return;
    }

    /* Setup server parameters. */
    memset(&connection, 0, sizeof(connection));
    connection.sin_addr.s_addr = inet_addr(serverIP);
    connection.sin_family = AF_INET;
    connection.sin_port = htons(streamPort);

    /* connect to server */
    if (::bind(sock, (struct sockaddr *)&connection, sizeof(connection)) < 0) {
        return;
    }

    int packetSize = MIN_MTU;
    int i, j, k, bytes;

    /* Start receiving images */
    while(1)
    {
        // try to receive windowsize number of packets

        bytes = 0;

        // get udp packet
        while (bytes != 576)
        {
            bytes = ::read(sock, clientData.imgobj.udppktdata.bytes, packetSize);
        }

        // copy the packet
        int frameNum = clientData.imgobj.udppktdata.pkt.frame;
        int seqNum = clientData.imgobj.udppktdata.pkt.sequence;
        int j = seqNum * DATA_CHUNK_SIZE;

        memcpy((void*) &readBuffer[frameNum][j], (void*)&clientData.imgobj.udppktdata.pkt.data, 576);

        pktsReceived[frameNum][seqNum] = 1;

        // check for the latest frame in the buffer ring
        i = currWind;
        do
        {
            int count = 0;
            for(j = 0; j < TOTAL_NUM_UDP_PKTS; j++)
                count = count + pktsReceived[i][j];
            if(count == TOTAL_NUM_UDP_PKTS)
            {
                latestFrame = i;
                for(j = 0; j < TOTAL_NUM_UDP_PKTS; j++)
                    pktsReceived[i][j] = 0;
                currWind = (i+1) % windowSize;
                break;
            }
            i = (i + 1) % windowSize;
        } while (i != currWind);

        // check if there exists a complete frame in the buffer
        // if it does, copy it and slide the window
        // if not, continue to get more packets
        if(latestFrame != -1)
        {
            for(i = 0; i < sizeof(INT_PKT); i++)
                clientData.imgobj.packetData.bytes[i] = readBuffer[latestFrame][i];
            latestFrame = -1;
        }
        else continue;

        // create a new image to store the compressed image data
        IplImage *compImage;
        compImage = cvCreateImage(cvSize(clientData.imgobj.packetData.pkt.imageWidth, clientData.imgobj.packetData.pkt.imageHeight),
                                         clientData.imgobj.packetData.pkt.imageDepth, clientData.imgobj.packetData.pkt.imageChannels);
        cvZero(compImage);

        // copy the data received into the newly created image
        for (i = 0, k = 0; i < compImage->height; i++) {
           for (j = 0; j < compImage->width; j++) {
              ((uchar*)(compImage->imageData + i * compImage->widthStep))[j] = clientData.imgobj.packetData.pkt.imageData[k++];
           }
        }

        // create a new image to store the decompressed image data
        IplImage *decompImage = NULL;
        CvMat* compmat;

        // decompress the image data received and store it in decompImage
        // compmat only stores return value of the compressed CvMat type
        // it does not have any memory allocated and should not be released
        if((compmat = clientData.imgobj.decompress_image(&decompImage, compImage)) != NULL)
        {
            // if img already has memory allocated release it
            if(clientData.imgobj.img) clientData.imgobj.freeIplImage();
            // reallocate memory for img and clone the new image into img
            if(decompImage)
                clientData.imgobj.img = cvCloneImage(decompImage);
        }

        /*thread safe. */
        pthread_mutex_lock(&mutex_s);

        // convert the decompressed IplImage into QT's QImage format
        clientData.imgobj.cvToQt();

        clientData.telemetry[0] = clientData.imgobj.packetData.pkt.telemetry[0];
        clientData.telemetry[1] = clientData.imgobj.packetData.pkt.telemetry[1];
        clientData.telemetry[2] = clientData.imgobj.packetData.pkt.telemetry[2];
        clientData.telemetry[3] = clientData.imgobj.packetData.pkt.telemetry[3];
        clientData.isDataReady = 1;

        pthread_mutex_unlock(&mutex_s);

        // release the images created
        if(compImage) cvReleaseImage(&compImage);
        if(decompImage) cvReleaseImage(&decompImage);

        /* Have we terminated yet? */
        pthread_testcancel();

        /* No. Sleep */
        usleep(3000);
   }
}

void* Client::startStream(void *arg)
{
    static_cast<Client*>(arg)->streamThread();
}

void Client::controlThread(void)
{
    int i, bytes;
    struct sockaddr_in connection;

    /* Make this thread cancellable using pthread_cancel(). */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    /* Initialize parameters for command socket */
    sockfd = ::socket(PF_INET, SOCK_STREAM, 0);

    memset(&connection, 0, sizeof(connection));
    connection.sin_family = AF_INET;
    connection.sin_addr.s_addr = inet_addr(serverIP);
    connection.sin_port = htons(streamPort+1024);

    ::connect(sockfd, (struct sockaddr*)&connection, sizeof(connection));

    int commandsize = (int) (sizeof clientData.commandBuffer);
    int replysize   = (int) (sizeof clientData.reply);

    /* Send out commands when available and retrieve replies from the server */
    while(1)
    {
        if(clientData.isCommandReady)
        {
            pthread_mutex_lock(&mutex_c);

            bytes = 0;


            if(bytes = ::send(sockfd, clientData.commandBuffer,  commandsize, 0) == -1)
            {
                break;
            }

            clientData.isCommandReady = 0;

            bytes = 0;
            for(i = 0; i < replysize; i = i + bytes)
            {
                if((bytes = ::recv(sockfd, clientData.reply, replysize, 0)) == -1)
                {
                    break;
                }
            }
            clientData.isReplyReady = 1;
            pthread_mutex_unlock(&mutex_c);
        }
        /* Have we terminated yet? */
        pthread_testcancel();

        /* No. Sleep */
        usleep(3000);
    }
}

void* Client::startControl(void *arg)
{
   static_cast<Client*>(arg)->controlThread();
}
