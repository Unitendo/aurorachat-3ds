#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <3ds.h>
#include <fcntl.h>
#include <malloc.h>
#include <citro2d.h>

#define SERVER_URL "10.22.23.207"
#define SOCKET_PORT "7070"

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

MessageHistory history[500];
int msgCount = 0;

void append_message(char* username, char* message) {
    if (msgCount > 499) {
        msgCount = 0;
    }
    snprintf(history[msgCount].username, 40, "%s", username);
    snprintf(history[msgCount].message, 365, "%s", message);
    msgCount += 1;
}

void show_error(const char* errtext) {
    errorConf err;
    errorInit(&err, ERROR_TEXT, CFG_LANGUAGE_EN);
    errorText(&err, errtext);
    errorDisp(&err);
}

int scene = 2;
int selbtn = 2;

char username[21];
char password[21];

float chatscroll = 60.0;

int errcde = 0;
char errRes[30] = {0};

bool loggingIn = false;
bool registering = false;

bool die = false;

char servername[30] = {0};
char* roomname = "general";

static char buffer[4096];
static size_t bufferlen = 0;

void processline(char *line) {
    char *cmd = strtok(line, "|");
    char *param1 = strtok(NULL, "|");
    char *param2 = strtok(NULL, "|");
    
    if (cmd && param1 && param2) {
        size_t len = strlen(param2);
        
        if (!strcmp(cmd, "msg")) {
			printf("<%s>: %s\n", param1, param2);
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






int main(int argc, char* argv[])
{
	gfxInitDefault();
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    C3D_RenderTarget* top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	C3D_RenderTarget* bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    sbuffer = C2D_TextBufNew(4096);

	u32 *soc_buffer = memalign(0x1000, 0x100000);
    if (!soc_buffer) {
        printf("The soc buffer could not be allocated.");
    }
    if (socInit(soc_buffer, 0x100000) != 0) {
        printf("socInit failed.");
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("Failed to create socket.");
    }

    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(atoi(SOCKET_PORT));
    server.sin_addr.s_addr = inet_addr(SERVER_URL);

    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) != 0) {
        printf("Failed to connect to server.");
    }

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

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
                processline(line_start);
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
            if (hidKeysDown() & KEY_A) {
                if (selbtn == 3) {
                    char sender[80];
                    sprintf(sender, "login|%s|%s|\njoin|general|\nhistory|1000\n", username, password);
			        send(sock, sender, strlen(sender), flags);
                    loggingIn = true;
                    scene = 1;
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
            if (hidKeysDown() & KEY_A) {
                if (selbtn == 3) {
                    char sender[140];
                    sprintf(sender, "register|%s|%s|\nlogin|%s|%s|\njoin|general|", username, password, username, password);
			        send(sock, sender, strlen(sender), flags);
                    registering = true;
                    scene = 1;
                }
            }
        }


        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

        C2D_TargetClear(top, C2D_Color32(255, 255, 255, 255));
        C2D_TargetClear(bottom, C2D_Color32(0, 0, 255, 255));

		C2D_SceneBegin(top);

        if (hidKeysHeld() & KEY_UP) {
            chatscroll += 3;
        }
        if (hidKeysHeld() & KEY_DOWN) {
            chatscroll -= 3;
        }


        if (scene == 1) {
            float y = chatscroll;
            char totalmessage[500];
            for (int i = 0; i < msgCount; i++) {
                snprintf(totalmessage, 500, "<%s>: %s", history[i].username, history[i].message);
                DrawText(totalmessage, 5, y, 0, 0.5, 0.5, C2D_Color32(0, 0, 0, 255), true);
                y += 15;
                int timesToExt = 0;
                timesToExt = strlen(totalmessage) / 39;
                for (int i = 0; i < timesToExt; i++) {
                    y += 14;
                }
            }

            C2D_DrawRectSolid(0, 0, 0, 600, 29, C2D_Color32(255, 255, 255, 255));
            char serverinfo[100] = {0};
            if (servername != NULL)
                sprintf(serverinfo, "#%s [%s]", roomname, servername);
            if (serverinfo != NULL)
                DrawText(serverinfo, 5, 0, 0, 0.7, 0.7, C2D_Color32(0, 0, 0, 255), true);

        }

        if (scene == 2) {
            DrawText("Account", 160, 0, 0, 0.7, 0.7, C2D_Color32(0, 0, 0, 255), true);
            if (selbtn == 1) {
                C2D_DrawRectSolid(55, 100, 0, 120, 70, C2D_Color32(0, 0, 255, 255));
                C2D_DrawRectSolid(215, 100, 0, 120, 70, C2D_Color32(0, 0, 100, 255));
            }

            if (selbtn == 2) {
                C2D_DrawRectSolid(215, 100, 0, 120, 70, C2D_Color32(0, 0, 255, 255));
                C2D_DrawRectSolid(55, 100, 0, 120, 70, C2D_Color32(0, 0, 100, 255));
            }

            DrawText("Login", 90, 120, 0, 0.7, 0.7, C2D_Color32(255, 255, 255, 255), true);
            DrawText("Register", 240, 120, 0, 0.7, 0.7, C2D_Color32(255, 255, 255, 255), true);

            if (hidKeysDown() & KEY_LEFT) {
                selbtn = 1;
            }
            if (hidKeysDown() & KEY_RIGHT) {
                selbtn = 2;
            }

            if (hidKeysDown() & KEY_A) {
                if (selbtn == 1) {
                    scene = 3;
                }
                if (selbtn == 2) {
                    scene = 4;
                }
            }
        }

        if (scene == 3) {
            DrawText("Log In", 165, 0, 0, 0.7, 0.7, C2D_Color32(0, 0, 0, 255), true);
            if (selbtn == 1) {
                C2D_DrawRectSolid(55, 100, 0, 120, 70, C2D_Color32(0, 0, 255, 255));
                C2D_DrawRectSolid(215, 100, 0, 120, 70, C2D_Color32(0, 0, 100, 255));
                C2D_DrawRectSolid(135, 200, 0, 120, 70, C2D_Color32(0, 0, 100, 255));
            }

            if (selbtn == 2) {
                C2D_DrawRectSolid(215, 100, 0, 120, 70, C2D_Color32(0, 0, 255, 255));
                C2D_DrawRectSolid(55, 100, 0, 120, 70, C2D_Color32(0, 0, 100, 255));
                C2D_DrawRectSolid(135, 200, 0, 120, 70, C2D_Color32(0, 0, 100, 255));
            }

            if (selbtn == 3) {
                C2D_DrawRectSolid(215, 100, 0, 120, 70, C2D_Color32(0, 0, 100, 255));
                C2D_DrawRectSolid(55, 100, 0, 120, 70, C2D_Color32(0, 0, 100, 255));
                C2D_DrawRectSolid(135, 200, 0, 120, 70, C2D_Color32(0, 0, 255, 255));
            }

            DrawText("Username", 70, 120, 0, 0.7, 0.7, C2D_Color32(255, 255, 255, 255), true);
            DrawText("Password", 235, 120, 0, 0.7, 0.7, C2D_Color32(255, 255, 255, 255), true);
            DrawText("Login", 170, 205, 0, 0.7, 0.7, C2D_Color32(255, 255, 255, 255), true);

            if (hidKeysUp() & KEY_LEFT) {
                selbtn--;
            }
            if (hidKeysUp() & KEY_RIGHT) {
                selbtn++;
            }

            if (selbtn < 1) {
                selbtn = 3;
            }
            if (selbtn > 3) {
                selbtn = 1;
            }
        }

        if (scene == 4) {
            DrawText("Register", 160, 0, 0, 0.7, 0.7, C2D_Color32(0, 0, 0, 255), true);
            if (selbtn == 1) {
                C2D_DrawRectSolid(55, 100, 0, 120, 70, C2D_Color32(0, 0, 255, 255));
                C2D_DrawRectSolid(215, 100, 0, 120, 70, C2D_Color32(0, 0, 100, 255));
                C2D_DrawRectSolid(135, 200, 0, 120, 70, C2D_Color32(0, 0, 100, 255));
            }

            if (selbtn == 2) {
                C2D_DrawRectSolid(215, 100, 0, 120, 70, C2D_Color32(0, 0, 255, 255));
                C2D_DrawRectSolid(55, 100, 0, 120, 70, C2D_Color32(0, 0, 100, 255));
                C2D_DrawRectSolid(135, 200, 0, 120, 70, C2D_Color32(0, 0, 100, 255));
            }

            if (selbtn == 3) {
                C2D_DrawRectSolid(215, 100, 0, 120, 70, C2D_Color32(0, 0, 100, 255));
                C2D_DrawRectSolid(55, 100, 0, 120, 70, C2D_Color32(0, 0, 100, 255));
                C2D_DrawRectSolid(135, 200, 0, 120, 70, C2D_Color32(0, 0, 255, 255));
            }

            DrawText("Username", 70, 120, 0, 0.7, 0.7, C2D_Color32(255, 255, 255, 255), true);
            DrawText("Password", 235, 120, 0, 0.7, 0.7, C2D_Color32(255, 255, 255, 255), true);
            DrawText("Login", 170, 205, 0, 0.7, 0.7, C2D_Color32(255, 255, 255, 255), true);

            if (hidKeysUp() & KEY_LEFT) {
                selbtn--;
            }
            if (hidKeysUp() & KEY_RIGHT) {
                selbtn++;
            }

            if (selbtn < 1) {
                selbtn = 3;
            }
            if (selbtn > 3) {
                selbtn = 1;
            }
        }
        


		u32 kDown = hidKeysDown();
		if (kDown & KEY_START)
			break;

        C3D_FrameEnd(0);
	}

	gfxExit();
    if (sock != NULL)
        closesocket(sock);
}
