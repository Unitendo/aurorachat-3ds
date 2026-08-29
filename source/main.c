#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <3ds.h>
#include <fcntl.h>
#include <malloc.h>
#include <citro2d.h>

#define SERVER_URL "104.236.25.60"
#define SOCKET_PORT "7070"


void show_error(const char* errtext) {
    errorConf err;
    errorInit(&err, ERROR_TEXT, CFG_LANGUAGE_EN);
    errorText(&err, errtext);
    errorDisp(&err);
}



// Image loading
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static u32 next_pow2(u32 n) {
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n++;
    return n;
}

static u32 clamp(u32 n, u32 min, u32 max) {
    if (n < min) return min;
    if (n > max) return max;
    return n;
}

static u32 rgba_to_abgr(u32 px) {
    u8 r = (px >> 24) & 0xFF;
    u8 g = (px >> 16) & 0xFF;
    u8 b = (px >> 8) & 0xFF;
    u8 a = px & 0xFF;
    return (a << 24) | (b << 16) | (g << 8) | r;
}

C2D_Image load_png(const char *path) {
    int width, height, channels;
    u32 *rgba_raw = (u32 *)stbi_load(path, &width, &height, &channels, 4);
    if (!rgba_raw) {
        show_error("Image decoding failed on an image.\nPerhaps it couldn't be found?");
    }

    u32 px_count = width * height;
    
    C3D_Tex *tex = (C3D_Tex *)malloc(sizeof(C3D_Tex));
    Tex3DS_SubTexture *subtex = (Tex3DS_SubTexture *)malloc(sizeof(Tex3DS_SubTexture));

    u32 tex_width = clamp(next_pow2(width), 64, 1024);
    u32 tex_height = clamp(next_pow2(height), 64, 1024);

    C3D_TexInit(tex, tex_width, tex_height, GPU_RGBA8);
    C3D_TexSetFilter(tex, GPU_LINEAR, GPU_NEAREST);

    subtex->width = width;
    subtex->height = height;
    subtex->left = 0.0f;
    subtex->top = 1.0f;
    subtex->right = (float)width / (float)tex_width;
    subtex->bottom = 1.0f - ((float)height / (float)tex_height);

    memset(tex->data, 0, px_count * 4);
    for (u32 i = 0; i < height; i++) {
        for (u32 j = 0; j < width; j++) {
            u32 src_idx = i * width + j;
            u32 abgr_px = rgba_to_abgr(rgba_raw[src_idx]);

            u32 dst_offset = ((((j >> 3) * (tex_width >> 3) + (i >> 3)) << 6) + ((i & 1) | ((j & 1) << 1) | ((i & 2) << 1) | ((j & 2) << 2) | ((i & 4) << 2) | ((j & 4) << 3)));
            
            ((u32 *)tex->data)[dst_offset] = abgr_px;
        }
    }

    free(rgba_raw);

    C2D_Image image;
    image.tex = tex;
    image.subtex = subtex;
    return image;
}





C2D_TextBuf sbuffer;
C2D_Text stext;

void DrawText(char *text, float x, float y, int z, float scaleX, float scaleY, u32 color, bool wordwrap) {
//    if (!sbuffer) {return;}
    C2D_TextBufClear(sbuffer);
    C2D_TextParse(&stext, sbuffer, text);
    C2D_TextOptimize(&stext);
    float wordwrapsize = 320.0f;

    if (!wordwrap) {
        C2D_DrawText(&stext, C2D_WithColor, x, y, z, scaleX, scaleY, color);
    }
    if (wordwrap) {
        C2D_DrawText(&stext, C2D_WithColor | C2D_WordWrap, x, y, z, scaleX, scaleY, color, wordwrapsize);
    }
}

typedef struct {
    char username[40];
    char message[366];
} MessageHistory;

MessageHistory history[800];
int msgCount = 0;

void remove_message(int index) {
    if (index < 0 || index >= msgCount) return;

    memmove(&history[index], &history[index + 1], (msgCount - index - 1) * sizeof(MessageHistory));

    msgCount--;
}

void append_message(char* username, char* message) {
    if (msgCount > 798) {
        remove_message(0);
    }
    snprintf(history[msgCount].username, 40, "%s", username);
    snprintf(history[msgCount].message, 365, "%s", message);
    msgCount += 1;
}

int scene = 2;
int selbtn = 1;

char username[21];
char password[21];

float chatscroll = 150.0;

int errcde = 0;
char errRes[30] = {0};

bool loggingIn = false;
bool registering = false;

bool die = false;

char servername[30] = {0};
char roomname[40] = {0};

char motd[1000] = {0};

static char buffer[4096];
static size_t bufferlen = 0;

u32 themecolor;
u32 textcolor;
u32 textcolorinvert;

typedef struct {
    char name[30];
    u32 themecolor;
    u32 textcolor;
    u32 textcolorinvert;
    u32 btncolor;
    u32 selbtncolor;
} ThemeList;

ThemeList themes[50];
int themeCount = 0;
int currentTheme = 0;

typedef struct {
    char name[30];
} RoomList;

RoomList rooms[10];
int roomCount = 0;
int currentRoomID = 0;

void append_room(char* name) {
    sprintf(rooms[roomCount].name, name);
    roomCount++;
}

void append_theme(char* name, u32 themecolor, u32 textcolor, u32 textcolorinvert, u32 btncolor, u32 selbtncolor) {
    themes[themeCount].themecolor = themecolor;
    themes[themeCount].textcolor = textcolor;
    themes[themeCount].textcolorinvert = textcolorinvert;
    themes[themeCount].btncolor = btncolor;
    themes[themeCount].selbtncolor = selbtncolor;
    sprintf(themes[themeCount].name, name);
    themeCount++;
}

u32 btncolor(int btn) {
    if (selbtn == btn) {
        return themes[currentTheme].selbtncolor;
    } else {
        return themes[currentTheme].btncolor;
    }
}

int motdFrameCounter = 0;






int main(int argc, char* argv[])
{
    fsInit();
    romfsInit();
	gfxInitDefault();
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    C3D_RenderTarget* top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	C3D_RenderTarget* bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    sbuffer = C2D_TextBufNew(300000);

	u32 *soc_buffer = memalign(0x1000, 0x100000);
    if (!soc_buffer) {
        show_error("The soc_buffer could not be allocated.");
    }
    if (socInit(soc_buffer, 0x100000) != 0) {
        show_error("socInit failed.");
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        show_error("Could not create socket.");
    }

    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(atoi(SOCKET_PORT));
    server.sin_addr.s_addr = inet_addr(SERVER_URL);

    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) != 0) {
        show_error("Aurorachat failed to connect to the server.\nPlease close the app and try again, if the issue persists, the servers may be down.");
    }

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);


    append_theme("Light", C2D_Color32(255, 255, 255, 255), C2D_Color32(0, 0, 0, 255), C2D_Color32(255, 255, 255, 255), C2D_Color32(0, 0, 170, 255), C2D_Color32(0, 0, 255, 255));
    append_theme("Dark", C2D_Color32(0, 0, 0, 255), C2D_Color32(255, 255, 255, 255), C2D_Color32(255, 255, 255, 255), C2D_Color32(0, 0, 170, 255), C2D_Color32(0, 0, 255, 255));
    append_theme("Blue + Green", C2D_Color32(19, 22, 137, 255), C2D_Color32(255, 255, 255, 255), C2D_Color32(255, 255, 255, 255), C2D_Color32(0, 63, 53, 255), C2D_Color32(0, 138, 116, 255));
    append_theme("Green + Blue", C2D_Color32(0, 63, 53, 255), C2D_Color32(255, 255, 255, 255), C2D_Color32(255, 255, 255, 255), C2D_Color32(19, 22, 137, 255), C2D_Color32(19, 22, 180, 255));
    append_theme("Aurora Purple", C2D_Color32(108, 0, 152, 255), C2D_Color32(255, 255, 255, 255), C2D_Color32(255, 255, 255, 255), C2D_Color32(83, 0, 116, 255), C2D_Color32(0, 0, 0, 255));

    append_room("general");
    append_room("bots");

    append_room("Type a room name");
    append_room("DM a username");

	// Main loop
	while (aptMainLoop())
	{

		hidScanInput();

		fd_set readfds;

        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        char tempbuffer[1024];
        ssize_t len = recv(sock, tempbuffer, sizeof(tempbuffer) - 1, 0);
    
        if (len == 0) {
            show_error("Connection interrupted.\nThe app will now close.");
            die = true;
        }

        if (len < 0) {
            goto skip_recv;
        }
    
        tempbuffer[len] = '\0';
    
        if (bufferlen + len >= 4096) {
            bufferlen = 0;
        }

        memcpy(buffer + bufferlen, tempbuffer, len);
        bufferlen += len;
        buffer[bufferlen] = '\0';

        char *line_start = buffer;
        char *newline_pos;
    
        while ((newline_pos = strchr(line_start, '\n')) != NULL) {
            *newline_pos = '\0'; 
        
            if (newline_pos > line_start) {
                
                char* line = line_start;

    char *cmd = strtok(line, "|");
    char *param1 = strtok(NULL, "|");
    char *param2 = strtok(NULL, "|");
    
    if (cmd && param1) {
        
        if (!strcmp(cmd, "msg")) {
            append_message(param1, param2);
            char totalmessage[500];
            snprintf(totalmessage, 500, "<%s>: %s", history[msgCount - 1].username, history[msgCount - 1].message);
            chatscroll -= 15;
            int timesToExt = 0;
            timesToExt = strlen(totalmessage) / 39;
            for (int i = 0; i < timesToExt; i++) {
                chatscroll -= 14;
            }
        }

        if (!strcmp(cmd, "hello")) {
            sprintf(servername, "%s", param2);
		}

        if (!strcmp(cmd, "motd")) {
            sprintf(motd, "%s", param1);
		}

        if (!strcmp(cmd, "ipbanned")) {
            show_error("Your IP address is banned.\nThe app will now close.");
            die = true;
		}

        if (!strcmp(cmd, "ok")) {
            errcde = 1;
		}

        if (!strcmp(cmd, "err")) {
            errcde = 2;
            sprintf(errRes, "%s", param1);
		}

        if(!strcmp(param1, "banned") && (errcde = 2)) {
            char bnerror[150];
            sprintf(bnerror, "You are banned, reason:\n\n%s", param2);
            show_error(bnerror);
            die = true;
        }

                if (registering && (errcde == 2)) {
                    if (!strcmp(errRes, "user_exists")) {
                        show_error("Username is already taken.");
                        scene = 2;
                        registering = false;
                        goto freedom;
                    }
                    if (!strcmp(errRes, "register_failure")) {
                        show_error("Registration failed.\nTry a different username perhaps?");
                        scene = 2;
                        registering = false;
                        goto freedom;
                    }
                    if (!strcmp(errRes, "args_bad")) {
                        show_error("The username or password you specified are invalid.\nPlease try again with new credentials.");
                        scene = 2;
                        registering = false;
                        goto freedom;
                    }
                }

                if (loggingIn && (errcde == 2)) {
                    if (!strcmp(errRes, "bad_login")) {
                        show_error("The credentials you provided are invalid.\nPlease try again.");
                        scene = 2;
                        loggingIn = false;
                        goto freedom;
                    }
                    if (!strcmp(errRes, "args_bad")) {
                        show_error("The username or password you specified are invalid.\nPlease try again with new credentials.");
                        scene = 2;
                        loggingIn = false;
                        goto freedom;
                    }
                }

                freedom:
                errcde = 0;
                selbtn = 1;
    }













            }

            line_start = newline_pos + 1;
        }

        size_t remaining = bufferlen - (line_start - buffer);
        if (remaining > 0 && line_start != buffer) {
            memmove(buffer, line_start, remaining);
        }
        bufferlen = remaining;

        skip_recv:

        if (die == true) {
            break;
        }

        if (scene == 1) {
		    if (hidKeysDown() & KEY_A) {
			    char message[365];
			    SwkbdState swkbd;
                swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, 365);
                swkbdSetFeatures(&swkbd, SWKBD_PREDICTIVE_INPUT);
                swkbdSetHintText(&swkbd, "Enter a message...");

			    swkbdInputText(&swkbd, message, sizeof(message));

			    char toSend[400];

			    sprintf(toSend, "msg|%s|\n", message);
			    send(sock, toSend, strlen(toSend), flags);
		    }
        }

        if (scene == 7) {
		    if (hidKeysDown() & KEY_A && (selbtn == roomCount - 2)) {
			    char croomname[30];
			    SwkbdState swkbd;
                swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, 30);
                swkbdSetFeatures(&swkbd, SWKBD_PREDICTIVE_INPUT);
                swkbdSetHintText(&swkbd, "Type a custom room name...");

			    swkbdInputText(&swkbd, croomname, sizeof(croomname));

			    char sender[150];

			    memset(history, 0, sizeof(history));
                msgCount = 0;
                chatscroll = 150;
                append_message("Local", "Welcome to aurorachat!");
                sprintf(sender, "join|%s|\nhistory|1000|\n", croomname);
                send(sock, sender, strlen(sender), flags);
                sprintf(roomname, "%s", croomname);
                scene = 1;
		    }
            if (hidKeysDown() & KEY_A && (selbtn == roomCount - 1)) {
			    char croomname[30];
			    SwkbdState swkbd;
                swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, 30);
                swkbdSetFeatures(&swkbd, SWKBD_PREDICTIVE_INPUT);
                swkbdSetHintText(&swkbd, "Type someone's username...");

			    swkbdInputText(&swkbd, croomname, sizeof(croomname));

			    char sender[150];

			    memset(history, 0, sizeof(history));
                msgCount = 0;
                chatscroll = 150;
                append_message("Local", "Welcome to DMs! Please note that both users must be actively DMing each other at the same time for it to work.");
                sprintf(sender, "join|@%s|\nhistory|1000|\n", croomname);
                send(sock, sender, strlen(sender), flags);
                sprintf(roomname, "@%s", croomname);
                scene = 1;
		    }
        }


        if (scene == 3) {
            if (hidKeysDown() & KEY_A) {
                if (selbtn == 1) {
			        SwkbdState swkbd;
                    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 1, 20);
                    swkbdSetFeatures(&swkbd, SWKBD_PREDICTIVE_INPUT);
                    swkbdSetHintText(&swkbd, "Enter a username...");
                    if (username != NULL)
                        swkbdSetInitialText(&swkbd, username);

			        swkbdInputText(&swkbd, username, sizeof(username));
                }
            }
            if (hidKeysDown() & KEY_A) {
                if (selbtn == 2) {
			        SwkbdState swkbd;
                    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 1, 20);
                    swkbdSetFeatures(&swkbd, SWKBD_PREDICTIVE_INPUT);
                    swkbdSetHintText(&swkbd, "Enter a password...");
                    if (password != NULL)
                        swkbdSetInitialText(&swkbd, password);

			        swkbdInputText(&swkbd, password, sizeof(password));
                }
            }
            if (hidKeysUp() & KEY_A) {
                if (selbtn == 3) {
                    char sender[200];
                    sprintf(sender, "login|%s|%s|\njoin|general|\nhistory|1000\nmotd|\n", username, password);
			        send(sock, sender, strlen(sender), flags);
                    loggingIn = true;
                    scene = 5;
                    selbtn = 1;
                }
            }
        }

        if (scene == 4) {
            if (hidKeysDown() & KEY_A) {
                if (selbtn == 1) {
			        SwkbdState swkbd;
                    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 1, 20);
                    swkbdSetFeatures(&swkbd, SWKBD_PREDICTIVE_INPUT);
                    swkbdSetHintText(&swkbd, "Enter a username...");
                    if (username != NULL)
                        swkbdSetInitialText(&swkbd, username);

			        swkbdInputText(&swkbd, username, sizeof(username));
                }
            }
            if (hidKeysDown() & KEY_A) {
                if (selbtn == 2) {
			        SwkbdState swkbd;
                    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 1, 20);
                    swkbdSetFeatures(&swkbd, SWKBD_PREDICTIVE_INPUT);
                    swkbdSetHintText(&swkbd, "Enter a password...");
                    if (password != NULL)
                        swkbdSetInitialText(&swkbd, password);

			        swkbdInputText(&swkbd, password, sizeof(password));
                }
            }
            if (hidKeysUp() & KEY_A) {
                if (selbtn == 3) {
                    char sender[200];
                    sprintf(sender, "register|%s|%s|\nlogin|%s|%s|\njoin|general|\nhistory|1000|\nmotd\n", username, password, username, password);
			        send(sock, sender, strlen(sender), flags);
                    registering = true;
                    scene = 5;
                    selbtn = 1;
                }
            }
        }


        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

        C2D_TargetClear(top, themes[currentTheme].themecolor);
        C2D_TargetClear(bottom, themes[currentTheme].themecolor);

		C2D_SceneBegin(top);


        if (scene == 1) {
            C2D_SceneBegin(top);
            float y = chatscroll;
            char totalmessage[500];
            for (int i = 0; i < msgCount; i++) {
                snprintf(totalmessage, 500, "<%s>: %s", history[i].username, history[i].message);

                int timesToExt = strlen(totalmessage) / 39;
                int lineH = 15 + timesToExt * 14;

                if (y + lineH >= 0 && y <= 240)
                    DrawText(totalmessage, 5, y, 0, 0.5, 0.5, themes[currentTheme].textcolor, true);

                y += lineH;

                if (y > 240)
                    break;
            }

            C2D_DrawRectSolid(0, 0, 0, 600, 29, themes[currentTheme].themecolor);
            char serverinfo[100] = {0};
            if (servername != NULL)
                sprintf(serverinfo, "#%s [%s]", roomname, servername);
            if (serverinfo != NULL)
                DrawText(serverinfo, 5, 0, 0, 0.7, 0.7, themes[currentTheme].textcolor, true);

            C2D_SceneBegin(bottom);
            DrawText(": Send message\n: Scroll chat\n: Return to room selection", 5, 5, 0, 0.5, 0.5, themes[currentTheme].textcolor, true);

            if (hidKeysUp() & KEY_B) {
                scene = 7;
                selbtn = 1;
            }

            if (hidKeysHeld() & KEY_UP) {
                chatscroll += 3;
            }
            if (hidKeysHeld() & KEY_DOWN) {
                chatscroll -= 3;
            }
        }

        if (scene == 7) {
            C2D_SceneBegin(top);

            int menuscroll = 0;
            menuscroll = 50 * selbtn;

            for (int i = 0; i < roomCount; i++) {
                C2D_DrawRectSolid(0, 50 * i - menuscroll, 0, 500, 50, btncolor(i));

                if (i != 0) {
                    DrawText(rooms[i].name, 5, 50 * i + 15 - menuscroll, 0, 0.6, 0.6, themes[currentTheme].textcolorinvert, true);
                } else {
                    DrawText(rooms[i].name, 5, 15 - menuscroll, 0, 0.6, 0.6, themes[currentTheme].textcolorinvert, true);
                }
            }

            if (hidKeysDown() & KEY_DOWN) {
                selbtn++;
            }
            if (hidKeysDown() & KEY_UP) {
                selbtn--;
            }

            if (selbtn < 0) {
                selbtn = roomCount - 1;
            }
            if (selbtn > roomCount - 1) {
                selbtn = 0;
            }

            C2D_SceneBegin(bottom);
            DrawText(": Select\n: Navigate\n: Return to menu", 5, 5, 0, 0.5, 0.5, themes[currentTheme].textcolor, true);

            if (hidKeysDown() & KEY_A) {
                if (selbtn != roomCount - 1) {
                    char sender[150];
                    memset(history, 0, sizeof(history));
                    msgCount = 0;
                    chatscroll = 150;
                    append_message("Local", "Welcome to aurorachat!");
                    sprintf(sender, "join|%s|\nhistory|1000|\n", rooms[selbtn].name);
                    send(sock, sender, strlen(sender), flags);
                    sprintf(roomname, "%s", rooms[selbtn].name);
                    scene = 1;
                }
            }
            if (hidKeysDown() & KEY_B) {
                scene = 5;
                selbtn = 1;
            }
        }

        if (scene == 2) {
            C2D_SceneBegin(top);
            C2D_DrawRectSolid(0, 0, 0, 500, 50, C2D_Color32(0, 0, 20, 255));
            C2D_DrawRectSolid(0, 50, 0, 500, 50, btncolor(1));
            C2D_DrawRectSolid(0, 100, 0, 500, 50, btncolor(2));

            DrawText("Account", 167, 15, 0, 0.6, 0.6, themes[currentTheme].textcolorinvert, true);
            DrawText("Sign In", 170, 65, 0, 0.6, 0.6, themes[currentTheme].textcolorinvert, true);
            DrawText("Register", 165, 115, 0, 0.6, 0.6, themes[currentTheme].textcolorinvert, true);

            if (hidKeysDown() & KEY_DOWN) {
                selbtn++;
            }
            if (hidKeysDown() & KEY_UP) {
                selbtn--;
            }

            if (selbtn < 1) {
                selbtn = 2;
            }
            if (selbtn > 2) {
                selbtn = 1;
            }

            if (hidKeysDown() & KEY_A) {
                if (selbtn == 1) {
                    scene = 3;
                }
                if (selbtn == 2) {
                    scene = 4;
                }
            }

            C2D_SceneBegin(bottom);
        }

        if (scene == 3) {
            C2D_SceneBegin(top);
            C2D_DrawRectSolid(0, 0, 0, 500, 50, C2D_Color32(0, 0, 20, 255));
            C2D_DrawRectSolid(0, 50, 0, 500, 50, btncolor(1));
            C2D_DrawRectSolid(0, 100, 0, 500, 50, btncolor(2));
            C2D_DrawRectSolid(0, 150, 0, 500, 50, btncolor(3));
            C2D_DrawRectSolid(0, 200, 0, 500, 50, btncolor(4));

            DrawText("Sign In", 170, 15, 0, 0.6, 0.6, themes[currentTheme].textcolorinvert, true);
            DrawText("Username", 162, 65, 0, 0.6, 0.6, themes[currentTheme].textcolorinvert, true);
            DrawText("Password", 162, 115, 0, 0.6, 0.6, themes[currentTheme].textcolorinvert, true);
            DrawText("Login", 168, 165, 0, 0.6, 0.6, themes[currentTheme].textcolorinvert, true);
            DrawText("Go Back", 163, 215, 0, 0.6, 0.6, themes[currentTheme].textcolorinvert, true);

            C2D_SceneBegin(bottom);
            switch (selbtn) {
                case 1:
                    DrawText("Username", 120, 50, 0, 0.6, 0.6, themes[currentTheme].textcolor, true);
                    DrawText("Enter your account's username.", 50, 70, 0, 0.6, 0.6, themes[currentTheme].textcolor, true);
                    break;
                case 2:
                    DrawText("Password", 120, 50, 0, 0.6, 0.6, themes[currentTheme].textcolor, true);
                    DrawText("Enter your account's password.", 50, 70, 0, 0.6, 0.6, themes[currentTheme].textcolor, true);
                    break;
                case 3:
                    DrawText("Login", 140, 50, 0, 0.6, 0.6, themes[currentTheme].textcolor, true);
                    DrawText("Log in using the details you provided.", 30, 70, 0, 0.6, 0.6, themes[currentTheme].textcolor, true);
                    break;
            }

            if (hidKeysDown() & KEY_DOWN) {
                selbtn++;
            }
            if (hidKeysDown() & KEY_UP) {
                selbtn--;
            }

            if (selbtn < 1) {
                selbtn = 4;
            }
            if (selbtn > 4) {
                selbtn = 1;
            }

            if (hidKeysDown() & KEY_A) {
                if (selbtn == 4) {
                    scene = 2;
                }
            }
        }

        if (scene == 4) {
            C2D_SceneBegin(top);
            C2D_DrawRectSolid(0, 0, 0, 500, 50, C2D_Color32(0, 0, 20, 255));
            C2D_DrawRectSolid(0, 50, 0, 500, 50, btncolor(1));
            C2D_DrawRectSolid(0, 100, 0, 500, 50, btncolor(2));
            C2D_DrawRectSolid(0, 150, 0, 500, 50, btncolor(3));
            C2D_DrawRectSolid(0, 200, 0, 500, 50, btncolor(4));

            DrawText("Register", 165, 15, 0, 0.6, 0.6, themes[currentTheme].textcolorinvert, true);
            DrawText("Username", 162, 65, 0, 0.6, 0.6, themes[currentTheme].textcolorinvert, true);
            DrawText("Password", 162, 115, 0, 0.6, 0.6, themes[currentTheme].textcolorinvert, true);
            DrawText("Register", 162, 165, 0, 0.6, 0.6, themes[currentTheme].textcolorinvert, true);
            DrawText("Go Back", 163, 215, 0, 0.6, 0.6, themes[currentTheme].textcolorinvert, true);

            C2D_SceneBegin(bottom);
            switch (selbtn) {
                case 1:
                    DrawText("Username", 120, 50, 0, 0.6, 0.6, themes[currentTheme].textcolor, true);
                    DrawText("Enter a username.", 85, 70, 0, 0.6, 0.6, themes[currentTheme].textcolor, true);
                    break;
                case 2:
                    DrawText("Password", 120, 50, 0, 0.6, 0.6, themes[currentTheme].textcolor, true);
                    DrawText("Enter a password.", 85, 70, 0, 0.6, 0.6, themes[currentTheme].textcolor, true);
                    break;
                case 3:
                    DrawText("Register", 125, 50, 0, 0.6, 0.6, themes[currentTheme].textcolor, true);
                    DrawText("Sign up using the details you provided.", 15, 70, 0, 0.6, 0.6, themes[currentTheme].textcolor, true);
                    break;
            }

            if (hidKeysDown() & KEY_DOWN) {
                selbtn++;
            }
            if (hidKeysDown() & KEY_UP) {
                selbtn--;
            }

            if (selbtn < 1) {
                selbtn = 4;
            }
            if (selbtn > 4) {
                selbtn = 1;
            }

            if (hidKeysDown() & KEY_A) {
                if (selbtn == 4) {
                    scene = 2;
                }
            }
        }

        if (scene == 5) {
            C2D_SceneBegin(top);
            C2D_DrawRectSolid(0, 0, 0, 500, 50, C2D_Color32(0, 0, 20, 255));
            C2D_DrawRectSolid(0, 50, 0, 500, 50, btncolor(1));
            C2D_DrawRectSolid(0, 100, 0, 500, 50, btncolor(2));
            C2D_DrawRectSolid(0, 150, 0, 500, 50, btncolor(3));

            DrawText("Main Menu", 158, 15, 0, 0.6, 0.6, themes[currentTheme].textcolorinvert, true);
            DrawText("Chat", 174, 65, 0, 0.6, 0.6, themes[currentTheme].textcolorinvert, true);
            DrawText("Themes", 165, 115, 0, 0.6, 0.6, themes[currentTheme].textcolorinvert, true);
            DrawText("Log Out", 163, 165, 0, 0.6, 0.6, themes[currentTheme].textcolorinvert, true);

            if (hidKeysDown() & KEY_DOWN) {
                selbtn++;
            }
            if (hidKeysDown() & KEY_UP) {
                selbtn--;
            }

            if (selbtn < 1) {
                selbtn = 3;
            }
            if (selbtn > 3) {
                selbtn = 1;
            }

            C2D_SceneBegin(bottom);

            if (motd)
                DrawText(motd, 5, 5, 0, 0.6, 0.6, themes[currentTheme].textcolor, true);

            if (hidKeysDown() & KEY_A) {
                if (selbtn == 1) {
                    scene = 7;
                    selbtn = 0;
                }
                if (selbtn == 2) {
                    scene = 6;
                    selbtn = currentTheme;
                }
                if (selbtn == 3) {
                    scene = 2;
                }
            }
        }

        if (scene == 6) {
            C2D_SceneBegin(top);

            int menuscroll = 0;
            menuscroll = 50 * selbtn;

            for (int i = 0; i < themeCount; i++) {
                C2D_DrawRectSolid(0, 50 * i - menuscroll, 0, 500, 50, btncolor(i));

                if (i != 0) {
                    DrawText(themes[i].name, 5, 50 * i + 15 - menuscroll, 0, 0.6, 0.6, themes[currentTheme].textcolorinvert, true);
                } else {
                    DrawText(themes[i].name, 5, 15 - menuscroll, 0, 0.6, 0.6, themes[currentTheme].textcolorinvert, true);
                }
            }

            if (hidKeysDown() & KEY_DOWN) {
                selbtn++;
            }
            if (hidKeysDown() & KEY_UP) {
                selbtn--;
            }

            if (selbtn < 0) {
                selbtn = themeCount - 1;
            }
            if (selbtn > themeCount - 1) {
                selbtn = 1;
            }

            C2D_SceneBegin(bottom);
            DrawText(": Select\n: Navigate\n: Return to menu", 5, 5, 0, 0.5, 0.5, themes[currentTheme].textcolor, true);

            if (hidKeysDown() & KEY_A) {
                currentTheme = selbtn;
            }
            if (hidKeysDown() & KEY_B) {
                scene = 5;
                selbtn = 1;
            }
        }

        if (motdFrameCounter >= 3600) {
            send(sock, "motd|\n", strlen("motd|\n"), flags);
            motdFrameCounter = 0;
        }
        
        motdFrameCounter++;


		u32 kDown = hidKeysDown();
		if (kDown & KEY_START)
			break;

        C3D_FrameEnd(0);
	}

	gfxExit();
    if (sock != NULL)
        closesocket(sock);
}