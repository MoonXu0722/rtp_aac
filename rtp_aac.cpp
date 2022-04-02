/*
 * 作者：_JT_
 * 博客：https://blog.csdn.net/weixin_42462202
 */


#include "rtp.h"

#define AAC_FILE    "hama.aac"
#define CLIENT_PORT 9832
#define IP "172.16.0.162"
struct AdtsHeader
{
    unsigned int syncword;  //12 bit 同步字 '1111 1111 1111'，说明一个ADTS帧的开始
    unsigned int id;        //1 bit MPEG 标示符， 0 for MPEG-4，1 for MPEG-2
    unsigned int layer;     //2 bit 总是'00'
    unsigned int protectionAbsent;  //1 bit 1表示没有crc，0表示有crc
    unsigned int profile;           //1 bit 表示使用哪个级别的AAC
    unsigned int samplingFreqIndex; //4 bit 表示使用的采样频率
    unsigned int privateBit;        //1 bit
    unsigned int channelCfg; //3 bit 表示声道数
    unsigned int originalCopy;         //1 bit 
    unsigned int home;                  //1 bit 

    /*下面的为改变的参数即每一帧都不同*/
    unsigned int copyrightIdentificationBit;   //1 bit
    unsigned int copyrightIdentificationStart; //1 bit
    unsigned int aacFrameLength;               //13 bit 一个ADTS帧的长度包括ADTS头和AAC原始流
    unsigned int adtsBufferFullness;           //11 bit 0x7FF 说明是码率可变的码流

    /* number_of_raw_data_blocks_in_frame
     * 表示ADTS帧中有number_of_raw_data_blocks_in_frame + 1个AAC原始帧
     * 所以说number_of_raw_data_blocks_in_frame == 0
     * 表示说ADTS帧中有一个AAC数据块并不是说没有。(一个AAC原始帧包含一段时间内1024个采样及相关数据)
     */
    unsigned int numberOfRawDataBlockInFrame; //2 bit
};

static int parseAdtsHeader(uint8_t* in, struct AdtsHeader* res)
{
    static int frame_number = 0;
    memset(res, 0, sizeof(*res));

    if ((in[0] == 0xFF) && ((in[1] & 0xF0) == 0xF0))
    {
        res->id = ((unsigned int)in[1] & 0x08) >> 3;
        PRINTF("adts:id  %d\n", res->id);
        res->layer = ((unsigned int)in[1] & 0x06) >> 1;
        PRINTF("adts:layer  %d\n", res->layer);
        res->protectionAbsent = (unsigned int)in[1] & 0x01;
        PRINTF("adts:protection_absent  %d\n", res->protectionAbsent);
        res->profile = ((unsigned int)in[2] & 0xc0) >> 6;
        PRINTF("adts:profile  %d\n", res->profile);
        res->samplingFreqIndex = ((unsigned int)in[2] & 0x3c) >> 2;
        PRINTF("adts:sf_index  %d\n", res->samplingFreqIndex);
        res->privateBit = ((unsigned int)in[2] & 0x02) >> 1;
        PRINTF("adts:pritvate_bit  %d\n", res->privateBit);
        res->channelCfg = ((((unsigned int)in[2] & 0x01) << 2) | (((unsigned int)in[3] & 0xc0) >> 6));
        PRINTF("adts:channel_configuration  %d\n", res->channelCfg);
        res->originalCopy = ((unsigned int)in[3] & 0x20) >> 5;
        PRINTF("adts:original  %d\n", res->originalCopy);
        res->home = ((unsigned int)in[3] & 0x10) >> 4;
        PRINTF("adts:home  %d\n", res->home);
        res->copyrightIdentificationBit = ((unsigned int)in[3] & 0x08) >> 3;
        PRINTF("adts:copyright_identification_bit  %d\n", res->copyrightIdentificationBit);
        res->copyrightIdentificationStart = (unsigned int)in[3] & 0x04 >> 2;
        PRINTF("adts:copyright_identification_start  %d\n", res->copyrightIdentificationStart);
        res->aacFrameLength = (((((unsigned int)in[3]) & 0x03) << 11) |
            (((unsigned int)in[4] & 0xFF) << 3) |
            ((unsigned int)in[5] & 0xE0) >> 5);
        PRINTF("adts:aac_frame_length  %d\n", res->aacFrameLength);
        res->adtsBufferFullness = (((unsigned int)in[5] & 0x1f) << 6 |
            ((unsigned int)in[6] & 0xfc) >> 2);
        PRINTF("adts:adts_buffer_fullness  %d\n", res->adtsBufferFullness);
        res->numberOfRawDataBlockInFrame = ((unsigned int)in[6] & 0x03);
        PRINTF("adts:no_raw_data_blocks_in_frame  %d\n", res->numberOfRawDataBlockInFrame);

        return 0;
    }
    else
    {
        PRINTF("failed to parse adts header\n");
        return -1;
    }
}

static int createUdpSocket()
{
    int fd;
    int on = 1;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on));

    return fd;
}

static int rtpSendAACFrame(int socket, char* ip, int16_t port,
    struct RtpPacket* rtpPacket, uint8_t* frame, uint32_t frameSize)
{
    int ret;

    rtpPacket->payload[0] = 0x00;
    rtpPacket->payload[1] = 0x10;
    rtpPacket->payload[2] = (frameSize & 0x1FE0) >> 5; //高8位
    rtpPacket->payload[3] = (frameSize & 0x1F) << 3; //低5位

    memcpy(rtpPacket->payload + 4, frame, frameSize);

    ret = rtpSendPacket(socket, ip, port, rtpPacket, frameSize + 4);
    if (ret < 0)
    {
        PRINTF("failed to send rtp packet\n");
        return -1;
    }

    rtpPacket->rtpHeader.seq++;

    /*
     * 如果采样频率是44100
     * 一般AAC每个1024个采样为一帧
     * 所以一秒就有 44100 / 1024 = 43帧
     * 时间增量就是 44100 / 43 = 1025
     * 一帧的时间为 1 / 43 = 23ms
     */
    rtpPacket->rtpHeader.timestamp += 1025;

    return 0;
}
void init_winsock()
{
    WORD wVersionRequested;
    WSADATA wsaData;
    int err;
    wVersionRequested = MAKEWORD(2, 2);
    err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0) {
        return;
    }
    if (LOBYTE(wsaData.wVersion) != 2 ||
        HIBYTE(wsaData.wVersion) != 2) {
        WSACleanup();
        return;
    }
}
int main(int argc, char* argv[])
{
    int ret;
    int socket;
    uint8_t* frame;
    struct AdtsHeader adtsHeader;
    struct RtpPacket* rtpPacket;
#ifdef _WIN32
    init_winsock();
#endif

    FILE *fp = fopen(AAC_FILE, "rb");
    if (!fp)
    {
        PRINTF("failed to open %s\n", AAC_FILE);
        return -1;
    }

    socket = createUdpSocket();
    if (socket < 0)
    {
        PRINTF("failed to create udp socket\n");
        return -1;
    }

    frame = (uint8_t*)malloc(5000);
    rtpPacket = (struct RtpPacket*)malloc(5000);

    rtpHeaderInit(rtpPacket, 0, 0, 0, RTP_VESION, RTP_PAYLOAD_TYPE_AAC, 1, 0, 0, 0x32411);
    printf("play aac with vlc.sdp over vlc\n");
    while (1)
    {
        PRINTF("--------------------------------\n");

        ret = fread(frame, 7, 1, fp);
        if (ret <= 0)
        {
            fseek(fp, 0, SEEK_SET);
            continue;
        }

        if (parseAdtsHeader(frame, &adtsHeader) < 0)
        {
            PRINTF("parse err\n");
            break;
        }

        ret = fread(frame, adtsHeader.aacFrameLength - 7, 1, fp);
        if (ret < 0)
        {
            PRINTF("read err\n");
            break;
        }

        rtpSendAACFrame(socket, (char *)IP, CLIENT_PORT,
            rtpPacket, frame, adtsHeader.aacFrameLength - 7);
#ifdef _WIN32
        Sleep(23);
#else
        usleep(23000);
#endif
    }
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
    fclose(fp);

    free(frame);
    free(rtpPacket);

    return 0;
}
