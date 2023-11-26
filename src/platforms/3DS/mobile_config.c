#include "../../non_core/mobile.h"
#include "3DES/common.h"

static const char device_names[][7] = {
    "BLUE ", "YELLOW", "GREEN ", "RED  ", "PURPLE", "BLACK ", "PINK ", "GREY "
};

void addr_to_str(const struct mobile_addr *src, char *dest) {
    if (src && src->type == MOBILE_ADDRTYPE_IPV4) {
        const struct mobile_addr4 *a4 = (const struct mobile_addr4*)src;
        snprintf(dest, 16, "%03u.%03u.%03u.%03u",
            a4->host[0], a4->host[1], a4->host[2], a4->host[3]);
    } else {
        strcpy(dest, "NONE");
    }
}

void str_to_addr(const char *src, struct mobile_addr *dest) {
    if (src && *src) {
        struct mobile_addr4 *a4 = (struct mobile_addr4*)dest;
        a4->type = MOBILE_ADDRTYPE_IPV4;
        char *data[4];
        data[0] = malloc(16);
        strncpy(data[0], src, 15);
        data[0][15] = 0;
        int i = 0;
        while (i < 4) {
            char *tmp = strchr(data[i++], '.');
            if (!tmp) break;
            *tmp++ = 0;
            if (i < 4)
                data[i] = tmp;
        }
        if (i == 3) {
            data[3] = data[2];
            data[2] = data[1];
            data[1] = data[0] + 15;
        } else if (i == 2) {
            data[3] = data[1];
            data[2] = data[1] = data[0] + 15;
        } else if (i == 1) {
            data[3] = data[2] = data[1] = data[0] + 15;
        }
        for (i = 0; i < 4; ++i) {
            unsigned oct = 0;
            sscanf(data[i], "%u", &oct);
            a4->host[i] = (oct < 255 ? oct : 255);
        }
    } else {
        dest->type = MOBILE_ADDRTYPE_NONE;
    }
}

void draw_menu(int selected, const char *device_name, bool unmetered,
    const char *dns1_str, const char *dns2_str, const char *relay_str,
    const char *port_str, bool token_set) {
    printf(RESET_TO_TOP_LEFT "Mobile Adapter Config\n");
    for (int i = 0; i < 8; ++i) {
        char str[32];
        switch (i) {
        case 0:
            snprintf(str, 32, "DEVICE TYPE: < %6.6s >", device_name);
            break;
        case 1:
            snprintf(str, 32, "UNMETERED  : [%c]", unmetered ? 'X' : ' ');
            break;
        case 2:
        case 3:
            snprintf(str, 32, "DNS ADDR %u : %s", i - 1, i == 2 ? dns1_str : dns2_str);
            break;
        case 4:
            snprintf(str, 32, "RELAY ADDR : %s", relay_str);
            break;
        case 5:
            snprintf(str, 32, "P2P PORT   : %s", port_str);
            break;
        case 6:
            snprintf(str, 32, "RELAY TOKEN: %sSET", token_set ? "" : "NOT ");
            break;
        case 7:
            strcpy(str, "OK");
            break;
        }
        printf("  %s%s" RESET "           \n", i == selected ? BLACK_ON_WHITE : "", str);
    }
}

struct callback_user {
    unsigned char *token;
    bool *token_set;
};

SwkbdCallbackResult set_token(void *user, const char **msg, const char *text, size_t length) {
    struct callback_user *cb_user = user;
    *cb_user->token_set = false;
    if (!length) return SWKBD_CALLBACK_OK;
    if (length != MOBILE_RELAY_TOKEN_SIZE * 2) {
        *msg = "This token is too short.";
        return SWKBD_CALLBACK_CONTINUE;
    }
    for (int i = 0; i < length; ++i) {
        unsigned char data = text[i];
        if (data >= '0' && data <= '9') data -= '0';
        else if (data >= 'A' && data <= 'F') data -= ('A' - 0xA);
        else if (data >= 'a' && data <= 'f') data -= ('a' - 0xA);
        else {
            *msg = "This token contains invalid characters.";
            return SWKBD_CALLBACK_CONTINUE;
        }
        if (i % 2) {
            cb_user->token[i / 2] |= data;
        } else {
            cb_user->token[i / 2] = data << 4;
        }
    }
    *cb_user->token_set = true;
    return SWKBD_CALLBACK_OK;
}

int MobileConf(enum mobile_adapter_device *device, bool *unmetered,
    struct mobile_addr *dns1, struct mobile_addr *dns2,
    struct mobile_addr *relay, unsigned *p2p_port,
    unsigned char *token, bool *token_set) {
    PrintConsole topScreen;

    gfxInitDefault();
    consoleInit(GFX_TOP, &topScreen);
    consoleSelect(&topScreen);

    const char *device_name = device_names[(int)*device - (int)MOBILE_ADAPTER_BLUE];
    char dns1_str[16]; addr_to_str(dns1, dns1_str);
    char dns2_str[16]; addr_to_str(dns2, dns2_str);
    char relay_str[16]; addr_to_str(relay, relay_str);
    char port_str[6]; snprintf(port_str, 6, "%hu", (uint16_t)*p2p_port);
    char token_str[MOBILE_RELAY_TOKEN_SIZE * 2 + 1] = {0};
    for (int i = 0; *token_set && i < MOBILE_RELAY_TOKEN_SIZE; ++i) {
        snprintf(token_str + i * 2, 3, "%02x", token[i]);
    }

    int selected = 0;

    draw_menu(selected, device_name, *unmetered, dns1_str, dns2_str,
        relay_str, port_str, *token_set);

    while (aptMainLoop()) {
        gspWaitForVBlank();
        hidScanInput();

        u32 kDown = hidKeysDown();

        if (kDown & KEY_START) {
            return 0;
        }

        if (kDown & KEY_DOWN) {
            selected = (selected + 1) % 8;
            draw_menu(selected, device_name, *unmetered, dns1_str, dns2_str,
                relay_str, port_str, *token_set);
        }

        if (kDown & KEY_UP) {
            if (!selected--) selected = 7;
            draw_menu(selected, device_name, *unmetered, dns1_str, dns2_str,
                relay_str, port_str, *token_set);
        }

        if ((kDown & KEY_RIGHT) && !selected) {
            if ((int)*device == 0xF) {
                *device = MOBILE_ADAPTER_BLUE;
            } else {
                *device = (enum mobile_adapter_device)((int)*device + 1);
            }
            device_name = device_names[(int)*device - (int)MOBILE_ADAPTER_BLUE];
            draw_menu(selected, device_name, *unmetered, dns1_str, dns2_str,
                relay_str, port_str, *token_set);
        }

        if ((kDown & KEY_LEFT) && !selected) {
            if (*device == MOBILE_ADAPTER_BLUE) {
                *device = (enum mobile_adapter_device)0xF;
            } else {
                *device = (enum mobile_adapter_device)((int)*device - 1);
            }
            device_name = device_names[(int)*device - (int)MOBILE_ADAPTER_BLUE];
            draw_menu(selected, device_name, *unmetered, dns1_str, dns2_str,
                relay_str, port_str, *token_set);
        }

        if ((kDown & KEY_A) && selected) {
            SwkbdState swkbd;
            SwkbdButton button = SWKBD_BUTTON_NONE;
            switch (selected) {
            case 1:
                *unmetered = !*unmetered;
                break;
            case 2:
            case 3:
            case 4:
                char *str = (selected == 2 ? dns1_str : selected == 3 ? dns2_str : relay_str);
                struct mobile_addr *addr = (selected == 2 ? dns1 : selected == 3 ? dns2 : relay);
                swkbdInit(&swkbd, SWKBD_TYPE_NUMPAD, 2, 15);
                swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT, "None", false);
                swkbdSetFeatures(&swkbd, SWKBD_FIXED_WIDTH);
                swkbdSetNumpadKeys(&swkbd, L'.', 0);
                button = swkbdInputText(&swkbd, str, 16);
                if (button == SWKBD_BUTTON_RIGHT) {
                    str_to_addr(str, addr);
                    addr_to_str(addr, str);
                } else {
                    addr->type = MOBILE_ADDRTYPE_NONE;
                    strcpy(str, "NONE");
                }
                break;
            case 5:
                swkbdInit(&swkbd, SWKBD_TYPE_NUMPAD, 1, 5);
                swkbdSetFeatures(&swkbd, SWKBD_FIXED_WIDTH);
                swkbdInputText(&swkbd, port_str, 6);
                sscanf(port_str, "%u", p2p_port);
                if (*p2p_port > 65535) *p2p_port = 65535;
                snprintf(port_str, 6, "%u", *p2p_port);
                break;
            case 6:
                struct callback_user cb_user = {.token = token, .token_set = token_set};
                swkbdInit(&swkbd, SWKBD_TYPE_WESTERN, 2, MOBILE_RELAY_TOKEN_SIZE * 2);
                swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT, "Unset", false);
                swkbdSetFeatures(&swkbd, SWKBD_FIXED_WIDTH | SWKBD_MULTILINE);
                swkbdSetValidation(&swkbd, SWKBD_FIXEDLEN, SWKBD_FILTER_CALLBACK, 0);
                swkbdSetFilterCallback(&swkbd, set_token, &cb_user);
                swkbdSetInitialText(&swkbd, token_str);
                button = swkbdInputText(&swkbd, token_str, MOBILE_RELAY_TOKEN_SIZE * 2 + 1);
                if (button == SWKBD_BUTTON_RIGHT) {
                    for (int i = 0; *token_set && i < MOBILE_RELAY_TOKEN_SIZE; ++i) {
                        snprintf(token_str + i * 2, 3, "%02x", token[i]);
                    }
                } else {
                    *token_set = false;
                }
                break;
            case 7:
                return 1;
            }
            draw_menu(selected, device_name, *unmetered, dns1_str, dns2_str,
                relay_str, port_str, *token_set);
        }

        // Flush and swap framebuffers
        gfxFlushBuffers();
        gfxSwapBuffers();
    }
    return 0;
}
