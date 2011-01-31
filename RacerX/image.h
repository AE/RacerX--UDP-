#ifndef IMAGE_H
#define IMAGE_H

#include <QImage>
#include <opencv/cv.h>
#include <opencv/highgui.h>
#include <image.h>

#define MAX_IMGSIZE 99942
#define IMG_TYPE IPL_DEPTH_8U
#define MIN_MTU 576
#define DATA_CHUNK_SIZE (MIN_MTU - (2* sizeof(int)))
#define TOTAL_NUM_UDP_PKTS (int)(sizeof(INT_PKT) / DATA_CHUNK_SIZE) + 1
typedef struct int_packet INT_PKT; // Image and Telemetry Packet
typedef struct udp_int_packet UDP_INT_PKT; // packet containg chunks of INT_PKT
                                           // for transmission over UDP
typedef union udppktdata UDP_PKT_DATA; // to access the raw bytes of UDP_INT_PKT

struct int_packet {
    double telemetry[4];
    int imageSize;
    int imageWidth;
    int imageHeight;
    int imageDepth;
    int imageChannels;
    char imageData[MAX_IMGSIZE];
};

struct udp_int_packet {
  int frame;
  int sequence;
  unsigned char data[DATA_CHUNK_SIZE];  // total size of this struct should be no more than 576
};

class Image
{
public:
    Image();
    IplImage* img;
    QImage* temp;

    union {
      UDP_INT_PKT pkt;
      unsigned char bytes[sizeof(UDP_INT_PKT)];
    } udppktdata;

    union {
        INT_PKT pkt;
        unsigned char bytes[sizeof(INT_PKT)];
    } packetData;

    // functions
    void cvToQt();
    void freeIplImage(void);
    CvMat* compress_image(IplImage **dst_img, IplImage *src_img, int p[3]);
    CvMat* decompress_image(IplImage **dst_img, IplImage *src_img);

};

#endif // IMAGE_H
