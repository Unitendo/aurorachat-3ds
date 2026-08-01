#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <citro2d.h>
#include <malloc.h>
#include <opusfile.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>

#include <3ds/applets/swkbd.h>

#include <3ds/types.h>
#include <3ds/services/cfgu.h>

#define SERVER_URL "104.236.25.60"
#define HTTP_PORT "6767"
#define HTTP_OR_HTTPS "http"
#define SOCKET_PORT "3033"

char token[300];

/*

    HTTP(S) Functions
    Coded by: Virtualle
    Extra credit: devKitPro examples

*/


u32 size;
u32 siz;

u32 bw;

u8 *buf;

static ndspWaveBuf waveBufA;
static ndspWaveBuf waveBufB;
static bool usingA = true;
static u32 audioSize = 0;
static int16_t* audioData = NULL;

void audioInitBuffers() {
    memset(&waveBufA, 0, sizeof(waveBufA));
    memset(&waveBufB, 0, sizeof(waveBufB));

    waveBufA.data_vaddr = audioData;
    waveBufA.nsamples = audioSize / sizeof(int16_t);

    waveBufB.data_vaddr = audioData;
    waveBufB.nsamples = audioSize / sizeof(int16_t);

    ndspChnWaveBufAdd(1, &waveBufA);
}

volatile bool downloadDone;

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} cstring;

cstring cstr_new(const char* str) {
    cstring s = {0};
    if (str) {
        s.len = strlen(str);
        s.cap = s.len + 1;
        s.data = malloc(s.cap);
        if (s.data) strcpy(s.data, str);
    }
    return s;
}

void cstr_free(cstring* s) {
    if (s->data) free(s->data);
    s->data = NULL;
    s->len = s->cap = 0;
}

bool CHECK_RESULT(const char* name, Result res) {
    bool failed = R_FAILED(res);
    printf("%s: %s! (0x%08lX)\n", name, failed ? "failed" : "success", res);
    return failed;
}

bool download(const char* url_str, const char* path) {
    httpcContext context;
    u32 status = 0;
    cstring url = cstr_new(url_str);
    downloadDone = false;
    bool success = false;
    u8* data = NULL;

    while (true) {
        if (CHECK_RESULT("httpcOpenContext", httpcOpenContext(&context, HTTPC_METHOD_GET, url.data, 0))) goto cleanup;
        if (CHECK_RESULT("httpcSetSSLOpt",   httpcSetSSLOpt(&context, SSLCOPT_DisableVerify))) goto close;
        if (CHECK_RESULT("httpcSetKeepAlive", httpcSetKeepAlive(&context, HTTPC_KEEPALIVE_ENABLED))) goto close;
        if (CHECK_RESULT("httpcAddRequestHeaderField", httpcAddRequestHeaderField(&context, "User-Agent", "skibidi toilet"))) goto close;
        if (CHECK_RESULT("httpcAddRequestHeaderField", httpcAddRequestHeaderField(&context, "Connection", "Keep-Alive"))) goto close;

        if (CHECK_RESULT("httpcBeginRequest", httpcBeginRequest(&context))) goto close;

        if (CHECK_RESULT("httpcGetResponseStatusCode", httpcGetResponseStatusCode(&context, &status))) goto close;

        if ((status >= 301 && status <= 303) || (status >= 307 && status <= 308)) {
            char newurl[0x1000];
            if (CHECK_RESULT("httpcGetResponseHeader", httpcGetResponseHeader(&context, "Location", newurl, sizeof(newurl)))) goto close;
            cstr_free(&url);
            url = cstr_new(newurl);
            httpcCloseContext(&context);
            continue;
        }
        break;
    }

    if (status != 200) goto close;

    if (CHECK_RESULT("httpcGetDownloadSizeState", httpcGetDownloadSizeState(&context, NULL, &siz))) goto close;

    FS_Archive sdmcRoot;
    if (CHECK_RESULT("FSUSER_OpenArchive", FSUSER_OpenArchive(&sdmcRoot, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, "")))) goto close;

    Handle fileHandle;
    FS_Path filePath = fsMakePath(PATH_ASCII, path);
    if (CHECK_RESULT("FSUSER_OpenFile", FSUSER_OpenFile(&fileHandle, sdmcRoot, filePath, FS_OPEN_CREATE | FS_OPEN_READ | FS_OPEN_WRITE, FS_ATTRIBUTE_ARCHIVE))) {
        FSUSER_CloseArchive(sdmcRoot);
        goto close;
    }

    if (CHECK_RESULT("FSFILE_SetSize", FSFILE_SetSize(fileHandle, 0))) {
        FSFILE_Close(fileHandle);
        FSUSER_CloseArchive(sdmcRoot);
        goto close;
    }

    data = malloc(0x4000);
    if (!data) {
        FSFILE_Close(fileHandle);
        FSUSER_CloseArchive(sdmcRoot);
        goto close;
    }

    bw = 0;
    u32 readSize;
    Result ret;

    do {
        ret = httpcDownloadData(&context, data, 0x4000, &readSize);
        if (readSize > 0) {
            FSFILE_Write(fileHandle, NULL, bw, data, readSize, FS_WRITE_FLUSH);
            bw += readSize;
        }
    } while (ret == HTTPC_RESULTCODE_DOWNLOADPENDING);

    success = (ret == 0);

    FSFILE_Close(fileHandle);
    FSUSER_CloseArchive(sdmcRoot);

close:
    httpcCloseContext(&context);
cleanup:
    free(data);
    cstr_free(&url);
    downloadDone = true;
    return success;
}

void downloadThreadFunc(void* arg) {
    char** args = (char**)arg;
    char* url = args[0];
    char* path = args[1];

    download(url, path);

    free(url);
    free(path);
    free(args);
}

Thread startDownload(const char* url, const char* path) {
    char** args = malloc(2 * sizeof(char*));
    args[0] = strdup(url);
    args[1] = strdup(path);

    Thread thread = threadCreate(downloadThreadFunc, args, 0x8000, 0x38, -2, false);
    if (thread == NULL) {
        printf("Failed to create thread!\n");
        free(args[0]); free(args[1]); free(args);
    }
    return thread;
}


char* errors = "no error";

Result http_post(const char* url, const char* data) {
    Result ret = 0;
    httpcContext context;
    char *newurl = NULL;
    u32 statuscode = 0;
    u32 contentsize = 0, readsize = 0, size = 0;
    buf = NULL;
    u8 *lastbuf = NULL;

    do {
        ret = httpcOpenContext(&context, HTTPC_METHOD_POST, url, 0);
        if (ret != 0) break;

        ret = httpcSetSSLOpt(&context, SSLCOPT_DisableVerify);
        if (ret != 0) break;

        ret = httpcSetKeepAlive(&context, HTTPC_KEEPALIVE_ENABLED);
        if (ret != 0) break;

        ret = httpcAddRequestHeaderField(&context, "User-Agent", "aurorachat For Nintendo 3DS Systems (v6.0)");
        if (ret != 0) break;

        ret = httpcAddRequestHeaderField(&context, "Content-Type", "text/plain");
        if (ret != 0) break;

        ret = httpcAddRequestHeaderField(&context, "auth", token);

        ret = httpcAddPostDataRaw(&context, (u32*)data, strlen(data));
        if (ret != 0) break;

        ret = httpcBeginRequest(&context);
        if (ret != 0) break;

        ret = httpcGetResponseStatusCodeTimeout(&context, &statuscode, 670 * 1000 * 1000);
        if (ret != 0) break;

        if ((statuscode >= 301 && statuscode <= 303) || (statuscode >= 307 && statuscode <= 308)) {
            if (newurl == NULL) newurl = malloc(0x1000);
            if (newurl == NULL) { ret = -1; break; }

            ret = httpcGetResponseHeader(&context, "Location", newurl, 0x1000);
            url = newurl;
            httpcCloseContext(&context);
            continue;
        }

        if (statuscode != 200) {
            sprintf(errors, "HTTP Error: %lx\n", statuscode);
            ret = httpcDownloadData(&context, buf + size, 0x1000, &readsize);
            size += readsize;
            if (ret == (s32)HTTPC_RESULTCODE_DOWNLOADPENDING) {
                lastbuf = buf;
                buf = realloc(buf, size + 0x1000);
                if (buf == NULL) { free(lastbuf); ret = -1; break; }
            }
            if (ret == 0) {
                buf = realloc(buf, size);
                printf("Response: %s\n", buf);
            }
            ret = -2;
            break;
        }

        ret = httpcGetDownloadSizeState(&context, NULL, &contentsize);
        if (ret != 0) break;

        buf = (u8*)malloc(0x1000);
        if (buf == NULL) { ret = -1; break; }

        do {
            ret = httpcDownloadData(&context, buf + size, 0x1000, &readsize);
            size += readsize;
            if (ret == (s32)HTTPC_RESULTCODE_DOWNLOADPENDING) {
                lastbuf = buf;
                buf = realloc(buf, size + 0x1000);
                if (buf == NULL) { free(lastbuf); ret = -1; break; }
            }
        } while (ret == (s32)HTTPC_RESULTCODE_DOWNLOADPENDING);

        if (ret == 0) {
            buf = realloc(buf, size);
            printf("Response: %s\n", buf);
        }

    } while(0);

    if (newurl) free(newurl);
    httpcCloseContext(&context);
    return ret;
}

Result http_get(const char* url) {
    Result ret = 0;
    httpcContext context;
    char *newurl = NULL;
    u32 statuscode = 0;
    u32 contentsize = 0, readsize = 0, size = 0;
    buf = NULL;
    u8 *lastbuf = NULL;

    do {
        ret = httpcOpenContext(&context, HTTPC_METHOD_GET, url, 0);
        if (ret != 0) break;

        ret = httpcSetSSLOpt(&context, SSLCOPT_DisableVerify);
        if (ret != 0) break;

        ret = httpcSetKeepAlive(&context, HTTPC_KEEPALIVE_ENABLED);
        if (ret != 0) break;

        ret = httpcAddRequestHeaderField(&context, "User-Agent", "aurorachat For Nintendo 3DS Systems (v6.0)");
        if (ret != 0) break;

        ret = httpcAddRequestHeaderField(&context, "auth", token);
        if (ret != 0) break;

        ret = httpcBeginRequest(&context);
        if (ret != 0) break;

        ret = httpcGetResponseStatusCodeTimeout(&context, &statuscode, 670 * 1000 * 1000);
        if (ret != 0) break;

        if ((statuscode >= 301 && statuscode <= 303) || (statuscode >= 307 && statuscode <= 308)) {
            if (newurl == NULL) newurl = malloc(0x1000);
            if (newurl == NULL) { ret = -1; break; }

            ret = httpcGetResponseHeader(&context, "Location", newurl, 0x1000);
            url = newurl;
            httpcCloseContext(&context);
            continue;
        }

        if (statuscode != 200) {
            ret = -2;
            break;
        }

        ret = httpcGetDownloadSizeState(&context, NULL, &contentsize);
        if (ret != 0) break;

        buf = (u8*)malloc(0x1000);
        if (buf == NULL) { ret = -1; break; }

        do {
            ret = httpcDownloadData(&context, buf + size, 0x1000, &readsize);
            size += readsize;
            if (ret == (s32)HTTPC_RESULTCODE_DOWNLOADPENDING) {
                lastbuf = buf;
                buf = realloc(buf, size + 0x1000);
                if (buf == NULL) { free(lastbuf); ret = -1; break; }
            }
        } while (ret == (s32)HTTPC_RESULTCODE_DOWNLOADPENDING);

        if (ret == 0) {
            buf = realloc(buf, size);
            // Null terminate safely if it fits
            if (buf) {
                buf = realloc(buf, size + 1);
                buf[size] = '\0';
            }
        }

    } while(0);

    if (newurl) free(newurl);
    httpcCloseContext(&context);
    return ret;
}

Result postEndpoint(const char* endpoint, const char* data) { // wrapper for HTTP POSTing
    char fullHttpUrl[75];
    sprintf(fullHttpUrl, "%s://%s:%s/api/%s", HTTP_OR_HTTPS, SERVER_URL, HTTP_PORT, endpoint);
    return http_post(fullHttpUrl, data);
}

void httpPostThread(void* arg) {
    const char* url = ((char**)arg)[0];
    const char* data = ((char**)arg)[1];

    http_post(url, data);

    threadExit(0);
}


/*

    VCOI (Virtualle's Custom Opus Implementation)
    Coded by: Virtualle
    Extra Credit: libopusfile, devKitPro    


*/

#define SAMPLE_RATE 48000
#define CHANNELS 2
#define BUFFER_MS 120
#define SAMPLES_PER_BUF (SAMPLE_RATE * BUFFER_MS / 1000)
#define WAVEBUF_SIZE (SAMPLES_PER_BUF * CHANNELS * sizeof(int16_t))

ndspWaveBuf waveBufs[2];
int16_t *audioBuffer = NULL;
LightEvent audioEvent;
volatile bool quit = false;

int loadAudio(const char* filename) {
    FILE* f = fopen(filename, "rb");
    if (!f) return 1;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    fseek(f, 44, SEEK_SET);

    audioSize = size - 44;

    audioData = (int16_t*)linearAlloc(audioSize);
    if (!audioData) {
        fclose(f);
        return 0;
    }

    fread(audioData, 1, audioSize, f);
    fclose(f);

    DSP_FlushDataCache(audioData, audioSize);
	return 0;
}

bool fillBuffer(OggOpusFile *file, ndspWaveBuf *buf) {
    int total = 0;
    while (total < SAMPLES_PER_BUF) {
        int16_t *ptr = buf->data_pcm16 + (total * CHANNELS);
        int ret = op_read_stereo(file, ptr, (SAMPLES_PER_BUF - total) * CHANNELS);
        
        if (ret <= 0) {
            // End of stream or error, try to loop
            if (ret == OP_HOLE || ret == OPUS_INVALID_PACKET) {
                continue; // Skip invalid data, keep reading
            }
            // Attempt to seek to beginning of stream
            if (op_pcm_seek(file, 0) < 0) {
                break; // Failed to seek, can't loop
            }
            // After seeking, try to read again
            continue;
        }
        total += ret;
    }

    if (total == 0) return false; // No data filled (failed to recover)

    buf->nsamples = total;
    DSP_FlushDataCache(buf->data_pcm16, total * CHANNELS * sizeof(int16_t));
    ndspChnWaveBufAdd(0, buf);
    return true;
}

bool fillBufferNoLoop(OggOpusFile *file, ndspWaveBuf *buf) {
    int total = 0;
    while (total < SAMPLES_PER_BUF) {
        int16_t *ptr = buf->data_pcm16 + (total * CHANNELS);
        int ret = op_read_stereo(file, ptr, (SAMPLES_PER_BUF - total) * CHANNELS);
        
        if (ret <= 0) {
            // End of stream or error — try to loop
            if (ret == OP_HOLE || ret == OPUS_INVALID_PACKET) {
                continue; // Skip invalid data, keep reading
            }
            // Attempt to seek to beginning of stream
            break; // Failed to seek — can't loop
            // After seeking, try to read again
            continue;
        }
        total += ret;
    }

    if (total == 0) return false; // No data filled (failed to recover)

    buf->nsamples = total;
    DSP_FlushDataCache(buf->data_pcm16, total * CHANNELS * sizeof(int16_t));
    ndspChnWaveBufAdd(0, buf);
    return true;
}

void audioCallback(void *arg) {
    if (!quit) LightEvent_Signal(&audioEvent);
}

void audioThread(void *arg) {
    OggOpusFile *file = (OggOpusFile*)arg;
    while (!quit) {
        for (int i = 0; i < 2; i++) {
            if (waveBufs[i].status == NDSP_WBUF_DONE) {
                if (!fillBuffer(file, &waveBufs[i])) { quit = true; return; }
            }
        }
        svcSleepThread(10000000L);
//        LightEvent_Wait(&audioEvent);
    }
    return;
}

void playSFX(int16_t* samples, u32 nsamples) {
    ndspChnReset(1);
    ndspChnSetRate(1, SAMPLE_RATE);
    ndspChnSetFormat(1, NDSP_FORMAT_STEREO_PCM16);
    ndspChnWaveBufClear(1);

    ndspWaveBuf waveBuf;
    memset(&waveBuf, 0, sizeof(waveBuf));
    waveBuf.data_pcm16 = samples;
    waveBuf.nsamples = nsamples;
    waveBuf.looping = false;
    waveBuf.status = NDSP_WBUF_DONE;

    DSP_FlushDataCache(samples, nsamples * 4);
    ndspChnWaveBufAdd(1, &waveBuf);
}



/*

    Sprite Tap Detection
    Coded by: Virtualle
    Note: This is pretty flawed in a lot of cases, but it works to some extent.

*/

bool isSpriteTapped(C2D_Sprite* sprite, float scaleX, float scaleY) {
    static bool wasTouched = false;
    bool isTouched = (hidKeysHeld() & KEY_TOUCH);

    if (!wasTouched && isTouched) {
        touchPosition touch;
        hidTouchRead(&touch);
        
        float w = sprite->image.subtex->width * scaleX;
        float h = sprite->image.subtex->height * scaleY;
        
        float left = sprite->params.pos.x;
        float right = sprite->params.pos.x + w;
        float top = sprite->params.pos.y;
        float bottom = sprite->params.pos.y + h;

        if (touch.px >= left && touch.px <= right && 
            touch.py >= top && touch.py <= bottom) {
            wasTouched = true;
            return true;
        }
    }

    if (!isTouched) wasTouched = false;
    return false;
}




/*

    DrawText()
    Coded by: Virtualle
    Note: This is not very good, however it gets the job done fairly easily!

*/

C2D_TextBuf sbuffer;
C2D_Text stext;

void DrawText(char *text, float x, float y, int z, float scaleX, float scaleY, u32 color, bool wordwrap) {
//    if (!sbuffer) {return;}
    C2D_TextBufClear(sbuffer);
    C2D_TextParse(&stext, sbuffer, text);
    C2D_TextOptimize(&stext);
    float wordwrapsize = 290.0f;

    if (!wordwrap) {
        C2D_DrawText(&stext, C2D_WithColor, x, y, z, scaleX, scaleY, color);
    }
    if (wordwrap) {
        C2D_DrawText(&stext, C2D_WithColor | C2D_WordWrap, x, y, z, scaleX, scaleY, color, wordwrapsize);
    }
}

/*

    show_error
    Author: Virtualle and nebulagamez
    Note: Fairly simple, just to make error displaying easier

*/
void show_error(const char* errtext) {
    errorConf err;
    errorInit(&err, ERROR_TEXT, CFG_LANGUAGE_EN); // This initializes the error module with the language set as English, this way it doesn't get messy with a region-changed 3DS
    errorText(&err, errtext);
    errorDisp(&err);
}

/*


    Append Room
    Coded by: Virtualle
    Note: Appends a room to the room list.


*/

typedef struct {
    char username[30];
    char message[300];
} History;

typedef struct {
    char name[30];
    char description[200];
    History msgs[50];
    int msgCount;
    int curScroll;
} RoomList;

RoomList rooms[100];
int roomCount = 0;

void append_room(char* name, char* desc) {
    if (roomCount < 100) {
        strcpy(rooms[roomCount].name, name);
        strcpy(rooms[roomCount].description, desc);
        roomCount++;
    }
}

RoomList* getRoom(char* name) {
    for (int i = 0; i < roomCount; i++) {
        if (!strcmp(rooms[i].name, name)) {
            return &rooms[i];
        }
    }
    return NULL;
}

bool append_message(char* username, char* message, char* room) {
    RoomList* roomptr = getRoom(room);
    if (roomptr == NULL) return false;
    
    if (roomptr->msgCount > 48) {
        memset(roomptr->msgs, 0, sizeof(roomptr->msgs));
        roomptr->msgCount = 0;
        roomptr->curScroll = 0;
    }
    strcpy(roomptr->msgs[roomptr->msgCount].username, username);
    strcpy(roomptr->msgs[roomptr->msgCount].message, message);
    roomptr->msgCount++;
    roomptr->curScroll += 55;
    return true;
}

/*

    WriteToFile()
    Author: Virtualle
    Note: Pretty basic, just write something into a file using fprintf

*/

bool WriteToFile(char* path, char* text) {
    FILE *writehandle = fopen(path, "w");
    if (!writehandle) {
        fclose(writehandle);
        return false;
    }
    fprintf(writehandle, text);

    fclose(writehandle);
    return true;
}

char* readFileToBuffer(const char* filePath, u32* outSize) {
    Handle file;
    u64 fileSize = 0;
    char* buffer = NULL;

    Result res = FSUSER_OpenFileDirectly(&file, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""), fsMakePath(PATH_ASCII, filePath), FS_OPEN_READ, 0);   
    if (R_FAILED(res)) return NULL;

    FSFILE_GetSize(file, &fileSize);
    if (fileSize == 0) {
        FSFILE_Close(file);
        return NULL;
    }

    buffer = (char*)malloc(fileSize + 1);
    if (!buffer) {
        FSFILE_Close(file);
        return NULL;
    }

    u32 bytesRead;
    FSFILE_Read(file, &bytesRead, 0, buffer, fileSize);
    FSFILE_Close(file);

    buffer[bytesRead] = '\0';
    if (outSize) *outSize = bytesRead;

    return buffer;
}

C2D_SpriteSheet spriteSheet;
C2D_Sprite button;
C2D_Sprite button2;
C2D_Sprite button3;
C2D_Sprite button4;
C2D_Sprite loading;
C2D_Sprite loading1;
C2D_Sprite loading2;
C2D_Sprite loading3;
C2D_Sprite loading4;
C2D_Sprite loading5;
C2D_Sprite chattab;
C2D_Sprite infotab;
C2D_Sprite roomstab;
C2D_Sprite themestab;
C2D_Sprite settingstab;
C2D_Sprite bg;

int scene = 1;

char password[21];
char username[21];

bool showpassword = false;

char buftext[1024];

bool showpassjustpressed = false;

bool outdated = false;

bool touched = false;
int lastTouchX = 0;
float velX = 0;

int selectingRoom = 0;
int selectedRoom = 0;
float chatscroll = 0.0f;
char msg[300];


void audioLoopCheck() {
    if (waveBufA.status == NDSP_WBUF_DONE || waveBufA.status == NDSP_WBUF_FREE) {

        waveBufA.data_vaddr = audioData;
        waveBufA.nsamples = audioSize / sizeof(int16_t);

        ndspChnWaveBufAdd(1, &waveBufA);
    }

    if (waveBufB.status == NDSP_WBUF_DONE || waveBufB.status == NDSP_WBUF_FREE) {

        waveBufB.data_vaddr = audioData;
        waveBufB.nsamples = audioSize / sizeof(int16_t);

        ndspChnWaveBufAdd(1, &waveBufB);
    }
}

// I'm working on this, I may use it in the future for culling
bool onScreen(float objX, float objY) {
    float minX = 200;
    float maxX = 200;
    float minY = rooms[selectedRoom].curScroll - 200;
    float maxY = rooms[selectedRoom].curScroll + 200;

    return (objX >= minX && objX <= maxX && objY >= minY && objY <= maxY);
}


/*

    This is the main function, this is what code will run when you actually start the program.

*/


int main() {

    fsInit();
	romfsInit();
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    C3D_RenderTarget* top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	C3D_RenderTarget* bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    httpcInit(1 * 1024 * 1024);

	spriteSheet = C2D_SpriteSheetLoad("romfs:/gfx/sprites.t3x");
    C2D_SpriteFromImage(&loading, C2D_SpriteSheetGetImage(spriteSheet, 0));
    C2D_SpriteFromImage(&bg, C2D_SpriteSheetGetImage(spriteSheet, 6));

	sbuffer = C2D_TextBufNew(4096);

    u32 *soc_buffer = memalign(0x1000, 0x100000);
    if (!soc_buffer) {
        // placeholder
    }
    if (socInit(soc_buffer, 0x100000) != 0) {
        // placeholder
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        // placeholder
    }

    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(atoi(SOCKET_PORT));
    server.sin_addr.s_addr = inet_addr(SERVER_URL);

    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) != 0) {
        // placeholder
    }

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    ndspInit();
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnSetRate(0, SAMPLE_RATE);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    ndspSetCallback(audioCallback, NULL);

    // channel 1 for bgm
    ndspChnReset(1);
    ndspChnSetInterp(1, NDSP_INTERP_LINEAR);
    ndspChnSetRate(1, 22050);
    ndspChnSetFormat(1, NDSP_FORMAT_MONO_PCM16);

    int animCounter = 0;
    int loadingFrame = 0;
    int loadingTimer = 0;

    int scene = 1;
    srand(time(NULL)); //yeah
    // load and play music
    int sdmcFailed = loadAudio("sdmc:/auc.wav");
	if (sdmcFailed == 1) { 
		int x = rand() % 2;
		if (x == 1) {
			loadAudio("romfs:/auc.wav");
		}
		if (x == 2) {
			loadAudio("romfs:/bullshit3.wav");
		}
		
	}
    ndspChnWaveBufAdd(1, &waveBufA);
    ndspChnWaveBufAdd(1, &waveBufB);

    char* newsHeader = "Failed to load news header";
    char* newsDesc = "Failed to load news description";



    char buffer[1024] = {0};

    bool roomsAdded = false;

    

	while (aptMainLoop())
	{
		hidScanInput();


        audioLoopCheck();

        fd_set readfds;
        struct timeval timeout;

        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 10000; // 10ms

        ssize_t len = recv(sock, buffer, 1024-1, 0);
        if (len > 0) {
            if (len > 0 && (len < 1024)) {
                buffer[1023] = '\0';
                char* username = strtok(buffer, "|");
                char* message = strtok(NULL, "|");
                char* room = strtok(NULL, "|");

                if (username && message && room && (roomsAdded == true)) {
                    append_message(username, message, room);
                }
            }
        } else if (len == 0) {
            show_error("You've been disconnected.\n\nTry opening the app again.");
            return -1;
        } else {
            // nothing
        }

        if (scene == 3) {
            // Block input typing if we are on the special static Rules view
            if (strcmp(rooms[selectedRoom].name, "rules") != 0) {
                if (hidKeysDown() & KEY_A) {
                    SwkbdState swkbd;
                    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, 299);
                    swkbdSetFeatures(&swkbd, SWKBD_PREDICTIVE_INPUT);
                    swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY, 0, 0);
                    swkbdSetHintText(&swkbd, "Type a message...");

                    swkbdInputText(&swkbd, msg, sizeof(msg));
                    
                    char sender[400];
                    sprintf(sender, "%s|%s|", msg, rooms[selectedRoom].name);
                    postEndpoint("chat", sender);
                    if (buf == NULL) {
                        postEndpoint("chat", sender);
                        if (buf == NULL) {
                            postEndpoint("chat", sender);
                            if (buf == NULL) {
                                show_error("The server did not respond, it could be offline.\nTry again later.\n\nAurorachat will now close.");
                                break;
                            }
                        }
                    }
					if (buf != NULL) {
            			sprintf(buftext, "%s", buf);
            			if (strstr(buftext, "ERR_BANNED") != 0) {
                			strtok(buftext, "|");
                			char* reason = strtok(NULL, "|");
                			char displayErr[400];
                			sprintf(displayErr, "You have been banned mid-session.\n\nReason:\n%s\nPlease appeal at https://startendo.org/appeal", reason ? reason : "No reason specified");
                			show_error(displayErr);
                			break; // Kick user out of primary application lifecycle execution loop
            			} else if (strstr(buftext, "ERR_MUTED") != 0) {
                			show_error("Your message wasn't sent because you are currently muted.");
           			    }
        			}
                }
            }
        }

        if (scene == 5) {
            if (hidKeysDown() & KEY_A) {
                if (selectingRoom == 0) {
                    SwkbdState swkbd;
                    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, 20);
                    swkbdSetFeatures(&swkbd, SWKBD_PREDICTIVE_INPUT);
                    swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY, 0, 0);
                    swkbdSetHintText(&swkbd, "Enter a username...");
                    if (username[0] != '\0') {
                        swkbdSetInitialText(&swkbd, username);
                    }


                    swkbdInputText(&swkbd, username, sizeof(username));
                }
                if (selectingRoom == 1) {
                    SwkbdState swkbd;
                    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, 20);
                    swkbdSetFeatures(&swkbd, SWKBD_PREDICTIVE_INPUT);
                    swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY, 0, 0);
                    swkbdSetHintText(&swkbd, "Enter a password...");
                    if (password[0] != '\0') {
                        swkbdSetInitialText(&swkbd, password);
                    }

                    swkbdInputText(&swkbd, password, sizeof(password));
                }
            }
        }

        if (scene == 6) {
            if (hidKeysDown() & KEY_A) {
                if (selectingRoom == 0) {
                    SwkbdState swkbd;
                    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, 20);
                    swkbdSetFeatures(&swkbd, SWKBD_PREDICTIVE_INPUT);
                    swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY, 0, 0);
                    swkbdSetHintText(&swkbd, "Enter a username...");
                    if (username[0] != '\0') {
                        swkbdSetInitialText(&swkbd, username);
                    }


                    swkbdInputText(&swkbd, username, sizeof(username));
                }
                if (selectingRoom == 1) {
                    SwkbdState swkbd;
                    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, 20);
                    swkbdSetFeatures(&swkbd, SWKBD_PREDICTIVE_INPUT);
                    swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY, 0, 0);
                    swkbdSetHintText(&swkbd, "Enter a password...");
                    if (password[0] != '\0') {
                        swkbdSetInitialText(&swkbd, password);
                    }

                    swkbdInputText(&swkbd, password, sizeof(password));
                }
            }
        }








		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

        C2D_TargetClear(top, C2D_Color32(255, 255, 255, 255));
        C2D_TargetClear(bottom, C2D_Color32(0, 0, 0, 255));

		C2D_SceneBegin(top);

        if (scene == 1) {

            C2D_SpriteSetScale(&bg, 2.5f, 1.0f);
            C2D_DrawSprite(&bg);
        
            DrawText("aurorachat", 129, 10, 0, 1.2f, 1.2f, C2D_Color32(215, 228, 255, 255), false);


            C2D_SceneBegin(bottom);
            C2D_DrawSprite(&bg);
            C2D_SpriteSetPos(&loading, 132, 95);
            C2D_DrawSprite(&loading);

            if (animCounter > 4 && loadingFrame < 5) {
                animCounter = 0;
                loadingFrame++;
                C2D_SpriteFromImage(&loading, C2D_SpriteSheetGetImage(spriteSheet, loadingFrame));
            } else if (loadingFrame >= 4) {
                loadingFrame = 0;
                C2D_SpriteFromImage(&loading, C2D_SpriteSheetGetImage(spriteSheet, loadingFrame));
            }

            if (loadingTimer >= 300) {
                loadingTimer = 0;
                scene = 4;
                if (postEndpoint("rooms", "{\"cmd\":\"CONNECT\", \"version\":\"6.0\"}") == 0) {
                    sprintf(buftext, "%s", buf);
                    char* roomcountertext = strtok(buftext, "|");
                    int roomsToAdd = atoi(roomcountertext);
                    for (int i = 0; i < roomsToAdd; i++) {
                        char* roomName = strtok(NULL, "|");
                        append_room(roomName, "i forgot");
                        append_message("System", "Welcome!", rooms[i].name);
                    }
                    
                    // Inject rules room context manually as a pseudo-room setup
                    append_room("rules", "Server Rules and Information");
                    
                    roomsAdded = true;
                }
            }


            loadingTimer++;
            animCounter++;
        }

        C2D_SceneBegin(bottom);

        if (scene == 2) {

            if (hidKeysUp() & KEY_DOWN) {
                if (selectingRoom < roomCount - 1) {
                    selectingRoom++;
                }
            }
            if (hidKeysUp() & KEY_UP) {
                if (selectingRoom > 0) {
                    selectingRoom--;
                }
            }

            C2D_SceneBegin(top);

            C2D_SpriteSetScale(&bg, 2.5f, 1.0f);
            C2D_DrawSprite(&bg);
        
            DrawText("aurorachat", 290, 2, 0, 0.8f, 0.8f, C2D_Color32(215, 228, 255, 255), false);
            DrawText("Rooms", 158, 5, 0, 1.1f, 1.1f, C2D_Color32(215, 228, 255, 255), false);
			DrawText("Please note that some of your messages may not", 5, 55, 0, 0.6f, 0.6f, C2D_Color32(215, 228, 255, 255), false);
			DrawText("appear on your end. Rest assured, they're going", 5, 75, 0, 0.6f, 0.6f, C2D_Color32(215, 228, 255, 255), false);
			DrawText("through!", 5, 95, 0, 0.6f, 0.6f, C2D_Color32(215, 228, 255, 255), false);
            DrawText("Move: ", 5, 200, 0, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 150), false);
            DrawText("Select: ", 5, 220, 0, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 150), false);


            C2D_SceneBegin(bottom);
            C2D_DrawSprite(&bg);

            for (int i = 0; i < roomCount; i++) {
                if (selectingRoom == i) {
                    C2D_DrawRectSolid(0, 26 * i, 0, 370, 26, C2D_Color32(0, 0, 0, 255));
                }
                DrawText(rooms[i].name, 10, 27 * i, 0, 0.7f, 0.7f, C2D_Color32(215, 228, 255, 255), false);
                
                if (hidKeysDown() & KEY_A) {
                    selectedRoom = selectingRoom;
                    
                    // Intercepting room transition if rules room is clicked
                    if (strcmp(rooms[selectedRoom].name, "rules") == 0) {
                        // Clear old contents completely
                        memset(rooms[selectedRoom].msgs, 0, sizeof(rooms[selectedRoom].msgs));
                        rooms[selectedRoom].msgCount = 0;
                        rooms[selectedRoom].curScroll = 0;
                        
                        // Fire GET Request to get current rules
                        char rulesUrl[128];
                        sprintf(rulesUrl, "%s://%s:%s/api/rules", HTTP_OR_HTTPS, SERVER_URL, HTTP_PORT);
                        
                        if (http_get(rulesUrl) == 0 && buf != NULL) {
                            // Assign downloaded text payload directly to rules layout
                            append_message("Server", (char*)buf, "rules");
                        } else {
                            append_message("System", "Could not fetch rules from server.", "rules");
                        }
                    }
                    scene = 3;
                }
            }

        }

        if (scene == 3) {

            if (hidKeysHeld() & KEY_DOWN) {
                rooms[selectedRoom].curScroll += 4;
            }
            if (hidKeysHeld() & KEY_UP) {
                rooms[selectedRoom].curScroll -= 4;
            }
            if (hidKeysDown() & KEY_B) {
                scene = 2;
            }

            C2D_SceneBegin(top);

            C2D_SpriteSetScale(&bg, 2.5f, 1.0f);
            C2D_DrawSprite(&bg);

            float msgY = 40.0f - rooms[selectedRoom].curScroll;
            for (int i = 0; i < rooms[selectedRoom].msgCount; i++) {
                DrawText(rooms[selectedRoom].msgs[i].username, 105, msgY, 0, 0.8f, 0.8f, C2D_Color32(215, 228, 255, 255), true);
                DrawText(rooms[selectedRoom].msgs[i].message, 110, msgY + 20, 0, 0.7f, 0.7f, C2D_Color32(215, 228, 255, 255), true);
                msgY += 60 + strlen(rooms[selectedRoom].msgs[i].message)/20 * 10;
            }
        
            DrawText("aurorachat", 290, 2, 0, 0.8f, 0.8f, C2D_Color32(215, 228, 255, 255), false);
            DrawText("Move: ", 5, 180, 0, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 150), false);
            DrawText("Leave: B", 5, 200, 0, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 150), false);
            if (strcmp(rooms[selectedRoom].name, "rules") != 0) {
                DrawText("Send a message: ", 5, 220, 0, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 150), false);
            }


            C2D_SceneBegin(bottom);
            C2D_DrawSprite(&bg);
            DrawText(rooms[selectedRoom].name, 10, 5, 0, 1.1f, 1.1f, C2D_Color32(215, 228, 255, 255), false);

            if (strcmp(rooms[selectedRoom].name, "rules") != 0) {
                DrawText("Press A to type a message...", 5, 220, 0, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 160), false);
            } else {
                DrawText("Viewing official system rules.", 5, 220, 0, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 160), false);
            }

        }

        if (scene == 4) {

            if (hidKeysUp() & KEY_DOWN) {
                if (selectingRoom < 1) {
                    selectingRoom++;
                }
            }
            if (hidKeysUp() & KEY_UP) {
                if (selectingRoom > 0) {
                    selectingRoom--;
                }
            }

            C2D_SceneBegin(top);

            C2D_SpriteSetScale(&bg, 2.5f, 1.0f);
            C2D_DrawSprite(&bg);
        
            DrawText("aurorachat", 290, 2, 0, 0.8f, 0.8f, C2D_Color32(215, 228, 255, 255), false);
            DrawText("Account Setup", 110, 5, 0, 1.1f, 1.1f, C2D_Color32(215, 228, 255, 255), false);
            DrawText("Move: ", 5, 200, 0, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 150), false);
            DrawText("Select: ", 5, 220, 0, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 150), false);


            C2D_SceneBegin(bottom);
            C2D_DrawSprite(&bg);

            C2D_DrawRectSolid(0, 26 * selectingRoom, 0, 370, 26, C2D_Color32(0, 0, 0, 255));
            DrawText("Create Account", 10, 27 * 0, 0, 0.7f, 0.7f, C2D_Color32(215, 228, 255, 255), false);
            DrawText("Log In", 10, 27 * 1, 0, 0.7f, 0.7f, C2D_Color32(215, 228, 255, 255), false);

            if (hidKeysDown() & KEY_A) {
                if (selectingRoom == 0) {
                    selectingRoom = 0;
                    scene = 5;
                }
                if (selectingRoom == 1) {
                    selectingRoom = 0;
                    scene = 6;
                }
            }

        }

        if (scene == 5) {

            if (hidKeysUp() & KEY_DOWN) {
                if (selectingRoom < 2) {
                    selectingRoom++;
                }
            }
            if (hidKeysUp() & KEY_UP) {
                if (selectingRoom > 0) {
                    selectingRoom--;
                }
            }
            if (hidKeysDown() & KEY_B) {
                scene = 4;
            }

            C2D_SceneBegin(top);

            C2D_SpriteSetScale(&bg, 2.5f, 1.0f);
            C2D_DrawSprite(&bg);
        
            DrawText("aurorachat", 290, 2, 0, 0.8f, 0.8f, C2D_Color32(215, 228, 255, 255), false);
            DrawText("Create Account", 110, 5, 0, 1.1f, 1.1f, C2D_Color32(215, 228, 255, 255), false);
            DrawText("Move: ", 5, 200, 0, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 150), false);
            DrawText("Select: ", 5, 220, 0, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 150), false);


            C2D_SceneBegin(bottom);
            C2D_DrawSprite(&bg);

            C2D_DrawRectSolid(0, 26 * selectingRoom, 0, 370, 26, C2D_Color32(0, 0, 0, 255));
            DrawText("Enter a username", 10, 27 * 0, 0, 0.7f, 0.7f, C2D_Color32(215, 228, 255, 255), false);
            DrawText("Enter a password", 10, 27 * 1, 0, 0.7f, 0.7f, C2D_Color32(215, 228, 255, 255), false);
            DrawText("Create account", 10, 27 * 2, 0, 0.7f, 0.7f, C2D_Color32(215, 228, 255, 255), false);

            if (hidKeysDown() & KEY_A) {
                if (selectingRoom == 2) {
                    selectingRoom = 0;
                    char sender[300];
                    sprintf(sender, "%s|%s|", username, password);
                    postEndpoint("signup", sender);
                    if (buf == NULL) {
                        postEndpoint("signup", sender);
                        if (buf == NULL) {
                            postEndpoint("signup", sender);
                            if (buf == NULL) {
                                show_error("The server never responded.\nTry again later.\n\nAurorachat will now close.");
                                break;
                            }
                        }
                    }
                    sprintf(buftext, "%s", buf);
                    if (strstr(buftext, "ERR_MISSING_INPUT") != 0) {
                        show_error("You need to enter BOTH fields.\nGo enter a username AND password then try again.");
                    } else if (strstr(buftext, "ERR_USER_USED") != 0) {
                        show_error("The username you chose is in use, please choose a different username.");
					} else if (strstr(buftext, "ERR_BANNED") != 0) {
            			strtok(buftext, "|"); 
            			char* reason = strtok(NULL, "|");
            			char displayErr[400];
            			sprintf(displayErr, "Your IP is banned from creating accounts.\n\nReason:\n%s\nPlease appeal at https://startendo.org/appeal", reason ? reason : "No reason specified");
            			show_error(displayErr);
                    } else {
                        scene = 6;
                    }
                }
            }

        }

        if (scene == 6) {

            if (hidKeysUp() & KEY_DOWN) {
                if (selectingRoom < 2) {
                    selectingRoom++;
                }
            }
            if (hidKeysUp() & KEY_UP) {
                if (selectingRoom > 0) {
                    selectingRoom--;
                }
            }
            if (hidKeysDown() & KEY_B) {
                scene = 4;
            }

            C2D_SceneBegin(top);

            C2D_SpriteSetScale(&bg, 2.5f, 1.0f);
            C2D_DrawSprite(&bg);
        
            DrawText("aurorachat", 290, 2, 0, 0.8f, 0.8f, C2D_Color32(215, 228, 255, 255), false);
            DrawText("Logging In", 120, 5, 0, 1.1f, 1.1f, C2D_Color32(215, 228, 255, 255), false);
            DrawText("Move: ", 5, 200, 0, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 150), false);
            DrawText("Select: ", 5, 220, 0, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 150), false);


            C2D_SceneBegin(bottom);
            C2D_DrawSprite(&bg);

            C2D_DrawRectSolid(0, 26 * selectingRoom, 0, 370, 26, C2D_Color32(0, 0, 0, 255));
            DrawText("Enter your username", 10, 27 * 0, 0, 0.7f, 0.7f, C2D_Color32(215, 228, 255, 255), false);
            DrawText("Enter your password", 10, 27 * 1, 0, 0.7f, 0.7f, C2D_Color32(215, 228, 255, 255), false);
            DrawText("Log In", 10, 27 * 2, 0, 0.7f, 0.7f, C2D_Color32(215, 228, 255, 255), false);

            if (hidKeysDown() & KEY_A) {
    			if (selectingRoom == 2) {
    				selectingRoom = 0;
        			char sender[300];
        			sprintf(sender, "%s|%s|", username, password);
        			postEndpoint("login", sender);
        			if (buf == NULL) {
            			postEndpoint("login", sender);
            			if (buf == NULL) {
                			postEndpoint("login", sender);
                			if (buf == NULL) {
                    			show_error("The server never responded.\nTry again later.\n\nAurorachat will now close.");
                    			break;
                			}
            			}
        			}
        			sprintf(buftext, "%s", buf);
        			if (strstr(buftext, "ERR_MISSING_INPUT") != 0) {
            			show_error("You need to enter BOTH fields.\nGo enter a username AND password then try again.");
        			} else if (strstr(buftext, "ERR_WRONG_PASS") != 0) {
            			show_error("You entered the wrong password.\nTry again.");
        			} else if (strstr(buftext, "ERR_BANNED") != 0) {
            			// Parse ban reason out of "ERR_BANNED|Reason|"
			            strtok(buftext, "|"); // Skips "ERR_BANNED"
            			char* reason = strtok(NULL, "|");
            			char displayErr[400];
            			sprintf(displayErr, "You have been banned from Aurorachat.\n\nReason:\n%s\nPlease appeal at https://startendo.org/appeal/", reason ? reason : "No reason specified");
            			show_error(displayErr);
        			} else {
            			char* intactToken = strtok(buftext, "|");
            			sprintf(token, "%s", intactToken);
            			scene = 2;
        			}
    			}
			}

        }

        




        C3D_FrameEnd(0);
	}

    socExit();
    httpcExit();
	gfxExit();
	return 0;
}
