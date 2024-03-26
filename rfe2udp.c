#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define WINUDP

#pragma comment(lib, "lib/FTD2XX.lib")
#ifdef WINUDP
#pragma comment(lib, "Ws2_32.lib")
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include "inc/ftd2xx.h"
#include "inc/version.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#if !defined(_WIN32) && (defined(__UNIX__) || (defined(__APPLE)))
#include <time.h>
#include <sys/time.h>
#include "inc/WinTypes.h"
#endif
#if !defined(_WIN32) && (defined(__UNIX__) || (defined(__APPLE__)))
#include <time.h>
#include <sys/time.h>
#include "inc/WinTypes.h"
#endif

// #define MLEN 65536
#define MLEN 131072
#define BYTESPERMS 8184
#define BYTESPERPKT 1023
#define PKTPERMS 8
#define MSPERSEC 1000
// #define DEBUG

typedef struct signmag2bit
{
  uint8_t loI : 2, loQ : 2, hiI : 2, hiQ : 2;
} signmag2bit;

int8_t expand2bitsignmag(uint8_t value)
{
  switch (value)
  {
  case 0:
    return 1;
    break;
  case 1:
    return 3;
    break;
  case 2:
    return -1;
    break;
  case 3:
    return -3;
    break;
  default:
    return 0;
    break;
  }
}
typedef struct
{
  uint8_t MSG[MLEN];
  int32_t SZE;
  int32_t CNT;
} PKT;

typedef struct
{
  uint32_t lComPortNumber;
  uint32_t ftDriverVer;
  FT_PROGRAM_DATA ftData;
  FT_HANDLE ftH;
} FT_CFG;

typedef struct
{
  FILE *ifp;
  FILE *ofp;
  char node[20]; // Node is the same as IP address
  char srvc[20]; // Service is the same as port
  uint16_t port; // Port can be 16 bits
  uint32_t IP;   // IP Address can be 32 bits
  char baseFname[64];
  char sampFname[64];
  char outFname[64];
  bool convertFile;
  bool logfile;
  bool binfile;
  bool useTimeStamp;
  bool FNHN;
  uint32_t sampMS;
  FT_CFG ftC;
  SWV V;
} CONFIG;

void fileSize(FILE *fp, int32_t *fs)
{
  fseek(fp, 0L, SEEK_END);
  *fs = ftell(fp);
  rewind(fp);
}
void printFTDIdevInfo(FT_CFG *ftC)
{
  fprintf(stdout, "ftDrvr:0x%x  ", ftC->ftDriverVer);
  fprintf(stderr, "FIFO:%s  ",
          (ftC->ftData.IFAIsFifo7 != 0) ? "Yes" : "No");
  if (ftC->lComPortNumber == -1)
  {
    fprintf(stderr, "No COM port assigned\n");
  }
  else
  {
    fprintf(stderr, "COM Port: %d ", ftC->lComPortNumber);
  }

  fprintf(stderr, "%s %s SN:%s \n", ftC->ftData.Description, ftC->ftData.Manufacturer,
          ftC->ftData.SerialNumber);
}

#if !defined(_WIN32)
void getISO8601(char datetime[17])
{
  struct timeval curTime;
  gettimeofday(&curTime, NULL);
  strftime(datetime, 17, "%Y%m%dT%H%M%SZ", gmtime(&curTime.tv_sec));
  //	return (datetime);
}
#endif

#if defined(_WIN32)
char *TimeNow(char *TimeString)
{
  SYSTEMTIME st;
  GetSystemTime(&st);
  sprintf(TimeString, "%.2d:%.2d:%.2d.%.3d",
          st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
  return TimeString;
}

void ISO8601(char *TimeString)
{
  SYSTEMTIME st;
  GetSystemTime(&st);
  sprintf(TimeString, "%.4d%.2d%.2dT%.2d%.2d%.2dZ",
          st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}
#endif

void initconfig(CONFIG *cfg)
{
  strcpy(cfg->baseFname, "L1IFDATA");
  strcpy(cfg->sampFname, "L1IFDATA.raw");
  strcpy(cfg->outFname, "L1IFDATA.bin");
  strcpy(cfg->node, "192.168.0.20");
  strcpy(cfg->srvc, "49152");
  cfg->port = 49152;
  cfg->logfile = false;
  cfg->binfile = false;
  cfg->useTimeStamp = false;
  cfg->FNHN = true;
  cfg->sampMS = 1;
}

void processArgs(int argc, char *argv[], CONFIG *cfg)
{
  static int len, i, ch = ' ';
  static char *usage =
      "usage: rfe2udp [ms] [options]\n"
      "       ms            how many milliseconds of data to collect\n"
      "       -f <filename> write to a different filename than the default\n"
      "       -l [filename] log raw data rather than binary interpretation\n"
      "       -r <filename> read raw log file and translate to binary\n"
      "       -t            use time tag for file name instead of default\n"
      "       -n            use FNLN instead of FNHN\n"
      "       -v            print version information\n"
      "       -?|h          show this usage infomation message\n"
      "  defaults: 1 ms of data logged in binary format as L1IFDATA.bin";

  if (argc > 1)
  {
    //    printf("%d %c\n", argc, argv[1][1]);
    for (i = 1; i < argc; i++)
    {
      if (argv[i][0] == '-')
      {
        ch = argv[i][1];
        switch (ch)
        {
        case '?':
          printf("%s", usage);
          exit(0);
          break;
        case 'h':
          printf("%s", usage);
          exit(0);
          break;
        case 'n':
          cfg->FNHN = false;
          break;
        case 'f':
          if (((i + 1) < argc) && (argv[i + 1][0] != '-'))
          {
            strcpy(cfg->outFname, argv[++i]);
          }
          else
          {
            printf("%s", usage);
            exit(1);
          }
          break;
        case 'l':
          if (((i + 1) < argc) && (argv[i + 1][0] != '-'))
          {
            strcpy(cfg->outFname, argv[++i]);
          }
          else
          {
            strcpy(cfg->outFname, cfg->sampFname);
          }
          cfg->logfile = true;
          break;
        case 'r':
          if (((i + 1) < argc) && (argv[i + 1][0] != '-'))
          {
            strcpy(cfg->sampFname, argv[++i]);
          }
          else
          {
            printf("%s", usage);
            exit(1);
          }
          cfg->convertFile = true;
          cfg->sampMS = 0;
          len = strlen(cfg->sampFname);
          strcpy(cfg->outFname, cfg->sampFname);
          cfg->outFname[len - 3] = '\0';
          strcat(cfg->outFname, "bin");
          break;
        case 't':
          cfg->useTimeStamp = true;
#if defined(_WIN32)
          ISO8601(cfg->baseFname); // ISO8601 for Windows platform
#endif
#if !defined(_WIN32)
          getISO8601(cfg->baseFname); // getISO8601 for GCC platforms
#endif
#ifdef DEBUG
          printf("%s\n", cfg->baseFname);
#endif
          break;
        case 'v':
          fprintf(stdout, "%s: GitCI:%s %s v%.1d.%.1d.%.1d\n",
                  cfg->V.Name, cfg->V.GitCI, cfg->V.BuildDate,
                  cfg->V.Major, cfg->V.Minor, cfg->V.Patch);
          if (cfg->ftC.ftH != 0)
            printFTDIdevInfo(&cfg->ftC);
          exit(0);
          break;
        default:
          printf("%s", usage);
          exit(0);
          break;
        }
      }
      else
      {
        cfg->sampMS = atoi(argv[i]);
        //        printf("ms:%d\n", cfg->sampMS);
      }
    }
  }
  else // argc not greater than 1
  {
    //    printf("Do I need this? Default params\n");
    cfg->sampMS = 1;
  }
  if (cfg->useTimeStamp == true)
  {
    strcpy(cfg->outFname, cfg->baseFname);
    if (cfg->logfile == true)
    {
      strcat(cfg->outFname, ".raw");
    }
    else
    {
      strcat(cfg->outFname, ".bin");
    }
  }
}

void readFTDIConfig(FT_CFG *cfg)
{
  FT_STATUS ftS;
  if (cfg->ftH != 0)
  {
    ftS = FT_GetDriverVersion(cfg->ftH, &cfg->ftDriverVer);
    if (ftS != FT_OK)
    {
      fprintf(stderr, "Couldn't read FTDI driver version.\n");
    }

    ftS = FT_SetTimeouts(cfg->ftH, 500, 500);
    if (ftS != FT_OK)
    {
      fprintf(stderr, "timeout A status not ok %d\n", ftS);
      // exit(1);
    }

    ftS = FT_EE_Read(cfg->ftH, &cfg->ftData);
    if (ftS != FT_OK)
    {
      fprintf(stderr, "FTDI EE Read did not succeed! %d\n", ftS);
    }

#if defined(_WIN32)
    ftS = FT_GetComPortNumber(cfg->ftH, &cfg->lComPortNumber);
    if (ftS != FT_OK)
    {
      fprintf(stderr, "FTDI Get Com Port Failed! %d\n", ftS);
    }
#endif
    ftS = FT_SetLatencyTimer(cfg->ftH, 2);
    // ftS = FT_SetUSBParameters(cfg->ftH, 0x10000, 0x10000);
    ftS = FT_SetUSBParameters(cfg->ftH, 0x02000, 0x02000); // 8192
    // ftS = FT_SetUSBParameters(cfg->ftH, 0x03000, 0x03000); // 16384
  }
}

void raw2bin(FILE *dst, FILE *src, bool FNHN)
{
  int32_t fSize = 0, idx = 0;
  signmag2bit data;
  int8_t valueToWrite = 0;
  fileSize(src, &fSize);
  //  printf("Convert file size: %d\n", fSize);

  for (idx = 0; idx < fSize; idx++)
  {
    fread(&data, 1, sizeof(signmag2bit), src);
    if (FNHN == true)
    {
      fputc(expand2bitsignmag((uint8_t)data.hiI), dst);
      fputc(expand2bitsignmag((uint8_t)data.hiQ), dst);
      fputc(expand2bitsignmag((uint8_t)data.loI), dst);
      fputc(expand2bitsignmag((uint8_t)data.loQ), dst);
    }
    else
    {
      fputc(expand2bitsignmag((uint8_t)data.loI), dst);
      fputc(expand2bitsignmag((uint8_t)data.loQ), dst);
      fputc(expand2bitsignmag((uint8_t)data.hiI), dst);
      fputc(expand2bitsignmag((uint8_t)data.hiQ), dst);
    }
  }
  fclose(src);
  fclose(dst);
}

void writeToBinFile(CONFIG *cfg, PKT *p)
{
  int32_t idx = 0;
  signmag2bit data;
  int8_t buff[131072];

  memset(buff, 0, 131072);
  for (idx = 0; idx < p->CNT; idx++)
  {
    memcpy(&data, &p->MSG[idx], 1);
    if (cfg->FNHN == true)
    {
      buff[4 * idx] = expand2bitsignmag(data.hiI);
      buff[4 * idx + 1] = expand2bitsignmag(data.hiQ);
      buff[4 * idx + 2] = expand2bitsignmag(data.loI);
      buff[4 * idx + 3] = expand2bitsignmag(data.loI);
    }
    else
    {
      buff[4 * idx] = expand2bitsignmag(data.loI);
      buff[4 * idx + 1] = expand2bitsignmag(data.loQ);
      buff[4 * idx + 2] = expand2bitsignmag(data.hiI);
      buff[4 * idx + 3] = expand2bitsignmag(data.hiQ);
    }
  }
  fwrite(buff, sizeof(int8_t), p->CNT * 4, cfg->ofp);
}

void convertFile(CONFIG *cfg)
{
  cfg->ifp = fopen(cfg->sampFname, "rb");
  if (cfg->ifp == NULL)
  {
    fprintf(stderr, "No such file %s\n", cfg->sampFname);
    exit(1);
  }
  else
  {
    cfg->ofp = fopen(cfg->outFname, "wb");
    if (cfg->ofp == NULL)
    {
      fprintf(stderr, "Can't open output file\n");
      exit(1);
    }
    else
    {
      raw2bin(cfg->ofp, cfg->ifp, cfg->FNHN);
    }
  }
  fclose(cfg->ifp);
  fclose(cfg->ofp);
  exit(0);
}

int32_t enQueue(PKT *src, PKT *dst, uint32_t cnt)
{
  uint32_t ext = cnt + dst->SZE;

  if (ext > MLEN)
  {
    fprintf(stderr, "OUT OF SPACE!\n");
    return -1;
  }
  memcpy(&dst->MSG[dst->SZE], src->MSG, cnt);
  dst->SZE += cnt;
  return dst->SZE;
}

int32_t deQueue(PKT *src, PKT *dst, uint32_t cnt)
{
  uint32_t ext = src->SZE - cnt;

  if (ext < 0)
  {
    fprintf(stderr, "REQUEST TOO LARGE!\n");
    return -1;
  }
  memcpy(dst->MSG, src->MSG, cnt);
  dst->SZE = dst->CNT = cnt;
  memmove(src->MSG, &src->MSG[cnt], ext);
  src->SZE = ext;
  return src->SZE;
}

int main(int argc, char *argv[])
{
#ifdef WINUDP
  WSADATA wsaData;
  int32_t iResult; // WSA
  PKT UDPtx, UDPrx;
  SOCKET UDPsock = INVALID_SOCKET;
  struct addrinfo *result = NULL, *ptr = NULL, hints;
  struct sockaddr_storage their_addr;
  socklen_t addr_size = sizeof their_addr;
//  const char *service = "27015";
//  short port = 27015;
//  const char *node = "192.168.0.20";
#endif
  CONFIG cnfg;
  char ManufacturerBuf[32];
  char ManufacturerIdBuf[16];
  char DescriptionBuf[64];
  char SerialNumberBuf[16];

  cnfg.ftC.ftData.Signature1 = 0x00000000;
  cnfg.ftC.ftData.Signature2 = 0xffffffff;
  cnfg.ftC.ftData.Version = 0x00000003; // 2232H extensions
  cnfg.ftC.ftData.Manufacturer = ManufacturerBuf;
  cnfg.ftC.ftData.ManufacturerId = ManufacturerIdBuf;
  cnfg.ftC.ftData.Description = DescriptionBuf;
  cnfg.ftC.ftData.SerialNumber = SerialNumberBuf;

  PKT rx, bfr, frame;
  FT_STATUS ftS;

  float sampleTime = 0.0;
  unsigned long i = 0, totalBytes = 0, targetBytes = 0, targetFrames = 0;
  unsigned char sampleValue;
  char valueToWrite;

  uint32_t idx = 0, mscount = 0, Nframes = 0;
  int32_t len = 0;
  uint8_t blankLine[120];
  uint8_t ch;

  cnfg.V.Major = MAJOR_VERSION;
  cnfg.V.Minor = MINOR_VERSION;
  cnfg.V.Patch = PATCH_VERSION;
#ifdef CURRENT_HASH
  strncpy(cnfg.V.GitCI, CURRENT_HASH, 40);
  cnfg.V.GitCI[40] = '\0';
#endif
#ifdef CURRENT_DATE
  strncpy(cnfg.V.BuildDate, CURRENT_DATE, 16);
  cnfg.V.BuildDate[16] = '\0';
#endif
#ifdef CURRENT_NAME
  strncpy(cnfg.V.Name, CURRENT_NAME, 10);
  cnfg.V.Name[10] = '\0';
#endif

  for (idx = 0; idx < 120; idx++)
  {
    blankLine[idx] = '\b';
  }
  blankLine[119] = '\0';

  memset(rx.MSG, 0, 65536);
  memset(bfr.MSG, 0, 65536);
  memset(frame.MSG, 0, 65536);
  bfr.CNT = bfr.SZE = 0;
  frame.CNT = frame.SZE = 0;

  initconfig(&cnfg);

  // ftS = FT_Open(0, &ftH);
  ftS = FT_OpenEx("USB<->GPS A", FT_OPEN_BY_DESCRIPTION, &cnfg.ftC.ftH);
  if (ftS != FT_OK)
  {
    fprintf(stderr, "Device not present. Perhaps just not plugged in?\n");
    // fprintf(stderr, "open device status not ok %d\n", ftS);
  }
  readFTDIConfig(&cnfg.ftC);
  processArgs(argc, argv, &cnfg);
  if (cnfg.ftC.ftH == 0)
    exit(0);

  if (cnfg.convertFile == true)
  {
    fprintf(stdout, "%s -> %s\n", cnfg.sampFname, cnfg.outFname);
    convertFile(&cnfg);
  }
  else
  {
    cnfg.ofp = fopen(cnfg.outFname, "wb");
  }

#ifdef DEBUG
  fprintf(stdout, "base:%s out:%s samp:%s ",
          cnfg.baseFname, cnfg.outFname, cnfg.sampFname);
  fprintf(stdout, "Raw? %s, TS:%s CF: %s FNHN? %s ",
          cnfg.logfile == true ? "yes" : "no",
          cnfg.useTimeStamp == true ? "yes" : "no",
          cnfg.convertFile == true ? "yes" : "no",
          cnfg.FNHN == true ? "yes" : "no");
  fprintf(stdout, "ms: %d\n", cnfg.sampMS);
#endif

  /* After Arguments Parsed, Open [Optional] Files
  targetBytes = BYTESPERMS * cnfg.sampMS;
  targetFrames = targetBytes / BYTESPERPKT;
  sampleTime = (float)(targetBytes / BYTESPERMS) / MSPERSEC;

  fprintf(stdout, "Collecting %10lu Bytes %10lu Frames (Nms*8184) [%6.3f sec] in %s\n",
          targetBytes, targetFrames, sampleTime, cnfg.outFname); */

#ifdef WINUDP
  iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
  // if (iResult != 0)
  if (iResult != NO_ERROR)
  {
    fprintf(stderr, "WSAStartup failed: %d\n", iResult);
    return 1;
  }
  else
  {
    memset(&UDPtx, 0, sizeof UDPtx);
    memset(&UDPrx, 0, sizeof UDPrx);
    UDPtx.SZE = (int32_t)(sizeof(UDPtx.MSG) - 1);
    UDPrx.SZE = (int32_t)(sizeof(UDPrx.MSG));

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    iResult = getaddrinfo(cnfg.node, cnfg.srvc, &hints, &result);
    if (iResult != 0)
    {
      printf("getaddrinfo failed with error: %d\n", iResult);
      WSACleanup();
      return 1;
    }

    for (ptr = result; ptr != NULL; ptr = ptr->ai_next)
    {
      UDPsock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
      if (UDPsock == INVALID_SOCKET)
      {
        printf("socket failed with error %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
      }

      iResult = bind(UDPsock, ptr->ai_addr, (int32_t)ptr->ai_addrlen);
      if (iResult != 0)
      {
        closesocket(UDPsock);
        UDPsock = INVALID_SOCKET;
        printf("bind failed with error: %d\n", WSAGetLastError());
        return 1;
      }
    }
    freeaddrinfo(result);

    printf("Receiving Datagrams on %s\n", cnfg.node);
    UDPrx.CNT = recvfrom(UDPsock, UDPrx.MSG, UDPrx.SZE, 0,
                         (SOCKADDR *)&their_addr, &addr_size);

    iResult = getpeername(UDPsock, (SOCKADDR *)&their_addr, &addr_size);
    if (iResult == -1)
    {
      printf("Nothing here #1\n");
    }
    if (UDPrx.CNT == SOCKET_ERROR)
    {
      printf("recvfrom failed with error: %d\n", WSAGetLastError());
    }
    UDPrx.MSG[UDPrx.CNT] = '\0';
    printf("Received: %s\n", UDPrx.MSG);
    cnfg.sampMS = atoi(UDPrx.MSG);
  }

  iResult = getpeername(UDPsock, (SOCKADDR *)&their_addr, &addr_size);
  if (iResult == -1)
  {
    printf("Nothing here #2\n");
  }
#endif

  /* After Arguments Parsed, Open [Optional] Files */
  targetBytes = BYTESPERMS * cnfg.sampMS;
  targetFrames = targetBytes / BYTESPERPKT;
  sampleTime = (float)(targetBytes / BYTESPERMS) / MSPERSEC;

  fprintf(stdout, "Collecting %10lu Bytes %10lu Frames (Nms*8184) [%6.3f sec] in %s\n",
          targetBytes, targetFrames, sampleTime, cnfg.outFname);

  ftS = FT_Purge(cnfg.ftC.ftH, FT_PURGE_RX | FT_PURGE_TX); // Purge both Rx and Tx buffers
  if (ftS != FT_OK)
  {
    fprintf(stderr, "Couldn't purge FTDI FIFO buffer! %d\n", ftS);
    fclose(cnfg.ofp);
    exit(1);
  }

  while (totalBytes < targetBytes)
  //while (1) // Need to keep it running, and take client requests
  {
    ftS = FT_GetQueueStatus(cnfg.ftC.ftH, &rx.CNT);
    rx.SZE = rx.CNT; // tell it you want the whole buffer
    ftS = FT_Read(cnfg.ftC.ftH, rx.MSG, rx.SZE, &rx.CNT);
    if (ftS != FT_OK)
    {
      fprintf(stderr, "FTDI status not OK %d\n", ftS);
      exit(1);
    }
    else
    {
      if ((rx.CNT < 65536) && (rx.CNT > 0))
      {
        enQueue(&rx, &bfr, rx.CNT);
        while (bfr.SZE > BYTESPERPKT)
        {
          deQueue(&bfr, &frame, BYTESPERPKT);
          if (Nframes++ < targetFrames) // When streaming, this is senseless
          {
#ifdef WINUDP
            UDPtx.CNT = sendto(UDPsock, frame.MSG, frame.SZE, 0,
                               (SOCKADDR *)&their_addr, addr_size);
#endif
            /* Write frame to UDP Socket */
            if (cnfg.logfile == true)
            {
              fwrite(frame.MSG, sizeof(uint8_t), BYTESPERPKT, cnfg.ofp);
            }
            if (cnfg.binfile == true)
            {
              writeToBinFile(&cnfg, &frame);
            }
          }
          else
          {
            break;
          }
        }

        if ((totalBytes + rx.CNT) > targetBytes)
        {
          rx.CNT = targetBytes - totalBytes;
        }
        totalBytes += rx.CNT;
      }                          // end buffer read not too big
      memset(rx.MSG, 0, rx.CNT); // May not be necessary
    }                            // end Read was not an error
    fprintf(stdout, "%s", blankLine);
    fprintf(stdout, "Collected: %10lu Bytes [%10lu left with %5d in queue] ",
            totalBytes, targetBytes - totalBytes, rx.CNT);
    fprintf(stdout, "NPkts %10d [%7.3f]", Nframes - 1, ((float)Nframes - 1) / 8000);
  } // end while loop

  if (FT_W32_PurgeComm(cnfg.ftC.ftH, PURGE_TXCLEAR | PURGE_RXCLEAR))
  {
    fprintf(stdout, "\n\t   %10lu Bytes written to %s\n",
            cnfg.logfile == true ? totalBytes : totalBytes * 2, cnfg.outFname);
  }
  ftS = FT_Close(cnfg.ftC.ftH);
  fclose(cnfg.ofp);

#ifdef WINUDP
  WSACleanup();
#endif
  return 0;
}