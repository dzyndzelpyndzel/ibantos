#include <stdint.h>

#define NULL ((void*)0)
#define MULTIBOOT_MAGIC 0x1BADB002
#define MULTIBOOT_FLAGS 0x0
#define MULTIBOOT_CHECKSUM -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)
#define VGA13_MEMORY 0xA0000

__attribute__((section(".multiboot")))
const uint32_t multiboot_header[] = {
    MULTIBOOT_MAGIC,
    MULTIBOOT_FLAGS,
    MULTIBOOT_CHECKSUM
};

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000

static volatile uint16_t* const vga_buffer = (volatile uint16_t*)VGA_MEMORY;

static inline void outb(uint16_t port, uint8_t val);

static int cursor_x = 0;
static int cursor_y = 0;
static int sp8lf_mode = 0;  // SP8LF mode: 0=Normal (black bg), 1=SP8LF (white bg)

// Update hardware cursor position
static void update_cursor(void) {
    uint16_t pos = cursor_y * VGA_WIDTH + cursor_x;
    outb(0x3D4, 14);         // Cursor location high byte
    outb(0x3D5, (pos >> 8) & 0xFF);
    outb(0x3D4, 15);         // Cursor location low byte
    outb(0x3D5, pos & 0xFF);
}

#define MAX_CMD_LEN 128

typedef unsigned int size_t;
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long  usize;
static char input_buffer[MAX_CMD_LEN];
static int input_pos = 0;
static int vga_color = 0x0E; // foreground=0x0E (yellow), background=0x00 (black)


static inline void vga_set_mode13h(void);
void init_framebuffer(uint32_t *framebuffer, int width, int height);
void draw_test_fb(void);
uint16_t vga_entry(char c, uint8_t color);
void vga_clear(void);
void vga_putc(char c);
void vga_write(const char* str);
void vga_set_color(uint8_t fg, uint8_t bg);
void vga_set_text_mode(void);
void vga_set_mode13h(void);
void calc_command(const char* cmd);

// ---------- minimal string functions ----------
int strncmp(const char* s1, const char* s2, int n) {
    for (int i = 0; i < n; i++) {
        if (s1[i] != s2[i] || s1[i] == 0 || s2[i] == 0) return s1[i] - s2[i];
    }
    return 0;
}

int strlen(const char* s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

int atoi(const char* str) {
    int res = 0;
    int i = 0;
    while (str[i] >= '0' && str[i] <= '9') {
        res = res * 10 + (str[i] - '0');
        i++;
    }
    return res;
}

// ---------- keyboard driver ---------

// Read a byte from an I/O port
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Write a byte to an I/O port
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

// Delay for approximately milliseconds using PIT
void delay_ms(unsigned int ms) {
    // PIT channel 0 data port
    #define PIT_CHANNEL0 0x40
    #define PIT_COMMAND 0x43
    
    // PIT operates at ~1193182 Hz
    // Configure PIT to mode 0 (interrupt on terminal count)
    outb(PIT_COMMAND, 0x30); // binary, mode 0, LSB then MSB, channel 0
    
    // Calculate divisor for desired delay (one-shot)
    unsigned int divisor = 1193182 / 1000; // 1 ms tick
    
    // Send divisor (LSB then MSB)
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
    
    // Wait for the required number of milliseconds
    for (unsigned int i = 0; i < ms; i++) {
        // Wait for PIT counter to reach zero (read status then read counter)
        unsigned char status;
        do {
            outb(PIT_COMMAND, 0xE2); // read-back command
            status = inb(PIT_CHANNEL0);
        } while (status & 0x80); // wait while output is high (counting down)
        
        // Reset counter for next millisecond
        outb(PIT_COMMAND, 0x30);
        outb(PIT_CHANNEL0, divisor & 0xFF);
        outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
    }
}

// Wait for a keypress and return its ASCII code
// Only handle make codes (key press)
// Track if Shift is pressed
static int shift_pressed = 0;

char get_char(void) {
    while (1) {
        uint8_t status = inb(0x64);
        if (status & 0x01) {
            uint8_t scancode = inb(0x60);

            // Handle key release
            if (scancode & 0x80) {
                // if release, check if Shift released
                if (scancode == 0xAA || scancode == 0xB6) shift_pressed = 0;
                continue;
            }

            // Handle Shift press
            if (scancode == 0x2A || scancode == 0x36) { // Left/Right Shift
                shift_pressed = 1;
                continue;
            }

            switch (scancode) {
                case 0x0F: return '\t'; // TAB = save
                case 0x1C: return '\n';    // Enter
                case 0x0E: return '\b';    // Backspace
                case 0x02: return shift_pressed ? '!' : '1';
                case 0x03: return shift_pressed ? '@' : '2';
                case 0x04: return shift_pressed ? '#' : '3';
                case 0x05: return shift_pressed ? '$' : '4';
                case 0x06: return shift_pressed ? '%' : '5';
                case 0x07: return shift_pressed ? '^' : '6';
                case 0x08: return shift_pressed ? '&' : '7';
                case 0x09: return shift_pressed ? '*' : '8';
                case 0x0A: return shift_pressed ? '(' : '9';
                case 0x0B: return shift_pressed ? ')' : '0';
                case 0x10: return shift_pressed ? 'Q' : 'q';
                case 0x11: return shift_pressed ? 'W' : 'w';
                case 0x12: return shift_pressed ? 'E' : 'e';
                case 0x13: return shift_pressed ? 'R' : 'r';
                case 0x14: return shift_pressed ? 'T' : 't';
                case 0x15: return shift_pressed ? 'Y' : 'y';
                case 0x16: return shift_pressed ? 'U' : 'u';
                case 0x17: return shift_pressed ? 'I' : 'i';
                case 0x18: return shift_pressed ? 'O' : 'o';
                case 0x19: return shift_pressed ? 'P' : 'p';
                case 0x1E: return shift_pressed ? 'A' : 'a';
                case 0x1F: return shift_pressed ? 'S' : 's';
                case 0x20: return shift_pressed ? 'D' : 'd';
                case 0x21: return shift_pressed ? 'F' : 'f';
                case 0x22: return shift_pressed ? 'G' : 'g';
                case 0x23: return shift_pressed ? 'H' : 'h';
                case 0x24: return shift_pressed ? 'J' : 'j';
                case 0x25: return shift_pressed ? 'K' : 'k';
                case 0x26: return shift_pressed ? 'L' : 'l';
                case 0x2C: return shift_pressed ? 'Z' : 'z';
                case 0x2D: return shift_pressed ? 'X' : 'x';
                case 0x2E: return shift_pressed ? 'C' : 'c';
                case 0x2F: return shift_pressed ? 'V' : 'v';
                case 0x30: return shift_pressed ? 'B' : 'b';
                case 0x31: return shift_pressed ? 'N' : 'n';
                case 0x32: return shift_pressed ? 'M' : 'm';
                case 0x39: return shift_pressed ? '\a' : ' ';
                case 0x0C: return shift_pressed ? '_' : '-';
                case 0x0D: return shift_pressed ? '+' : '=';
                case 0x33: return shift_pressed ? '<' : ',';
                case 0x34: return shift_pressed ? '>' : '.';
                case 0x35: return shift_pressed ? '?' : '/';
                case 0x27: return shift_pressed ? '"' : '\'';
                case 0x28: return shift_pressed ? ':' : ';';
                case 0x1A: return shift_pressed ? '{' : '[';
                case 0x1B: return shift_pressed ? '}' : ']';
                case 0x29: return shift_pressed ? '~' : '`';
                case 0x2B: return shift_pressed ? '|' : '\\';
                default: break;
            }
        }
    }
}




void _start(void);
void draw_test(void); // add this near the top with other prototypes
void grublmao(void);

// filesystem

#define MAX_CHILDREN 32
#define MAX_NAME_LEN 32

typedef struct fs_node {
    char name[MAX_NAME_LEN];
    int is_dir;

    struct fs_node* parent;

    struct fs_node* children[MAX_CHILDREN];
    int child_count;

    uint8_t* data;
    size_t size;
} fs_node;

static uint8_t heap[64 * 1024];
static size_t heap_pos = 0;

void* kmalloc(size_t size) {
    void* p = &heap[heap_pos];
    heap_pos += size;
    return p;
}
// dodaj przed fs_create_node
void* memset(void* dest, int val, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    for (size_t i = 0; i < n; i++) d[i] = (unsigned char)val;
    return dest;
}
void* memcpy(void* dest, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}


int strcmp(const char* a, const char* b) {
    while (*a && (*a == *b)) { a++; b++; }
    return *(unsigned char*)a - *(unsigned char*)b;
}

char* strncpy(char* dst, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = 0;
    return dst;
}


static int fs_edit_mode = 0;
static fs_node* fs_edit_file = 0;

static char fs_edit_buffer[1024];
static size_t fs_edit_pos = 0;
static fs_node* fs_root = 0;
static fs_node* fs_cwd = 0;


fs_node* fs_create_node(const char* name, int is_dir) {
    fs_node* n = kmalloc(sizeof(fs_node));
    memset(n, 0, sizeof(fs_node)); // zerowanie struktury
    strncpy(n->name, name, MAX_NAME_LEN - 1);
    n->is_dir = is_dir;
    return n;
}

void fs_init(void) {
    fs_root = fs_create_node("~", 1);
    fs_root->parent = fs_root;
    fs_cwd = fs_root;
}

void fs_mkdir(const char* name) {
    if (fs_cwd->child_count >= MAX_CHILDREN) {
        vga_write("dir full\n");
        return;
    }

    fs_node* d = fs_create_node(name, 1);
    d->parent = fs_cwd;
    fs_cwd->children[fs_cwd->child_count++] = d;
}
void fs_ls(void) {
    for (int i = 0; i < fs_cwd->child_count; i++) {
        fs_node* n = fs_cwd->children[i];
        vga_write(n->is_dir ? "\n[FOLDERs] -> " : "\n[FILES] -> ");
        vga_write(n->name);
        vga_write("\n");
    }
}
void fs_cd(const char* path) {
    fs_node* cur;

    if (strncmp(path, "~", 1) == 0) {
        cur = fs_root;
        path++;
        if (*path == '/') path++;
    } else {
        cur = fs_cwd;
    }

    while (*path) {
        if (strncmp(path, "..", 2) == 0) {
            cur = cur->parent;
            path += 2;
        } else {
            // read dir name
            char name[MAX_NAME_LEN];
            int i = 0;
            while (*path && *path != '/' && i < MAX_NAME_LEN - 1) {
                name[i++] = *path++;
            }
            name[i] = 0;

            int found = 0;
            for (int j = 0; j < cur->child_count; j++) {
                fs_node* n = cur->children[j];
                if (n->is_dir && strcmp(n->name, name) == 0) {
                    cur = n;
                    found = 1;
                    break;
                }
            }

            if (!found) {
                vga_write("folder/dir doesnt exist\n");
                return;
            }
        }

        if (*path == '/') path++;
    }

    fs_cwd = cur;
}
void fs_dir_from(fs_node* dir) {
    for (int i = 0; i < dir->child_count; i++) {
        fs_node* n = dir->children[i];
        if (n->is_dir) {
            vga_write(n->name);
            vga_write("\n");
        }
    }
}

void fs_mkfile(const char* name) {
    if (fs_cwd->child_count >= MAX_CHILDREN) {
        vga_write("folder is full\n");
        return;
    }

    fs_node* f = fs_create_node(name, 0);
    f->parent = fs_cwd;
    fs_cwd->children[fs_cwd->child_count++] = f;
    vga_write("\nmade file: ");
    vga_write(name);
    vga_write("\n");
}
void fs_delfile(const char* name) {
    for (int i = 0; i < fs_cwd->child_count; i++) {
        fs_node* n = fs_cwd->children[i];
        if (!n->is_dir && strcmp(n->name, name) == 0) {

            // shift children left
            for (int j = i; j < fs_cwd->child_count - 1; j++) {
                fs_cwd->children[j] = fs_cwd->children[j + 1];
            }

            fs_cwd->child_count--;

            vga_write("\nmade file: ");
            vga_write(name);
            vga_write("\n");
            return;
        }
    }
    vga_write("file was not found\n");
}

void fs_edfile_start(const char* name) {
    for (int i = 0; i < fs_cwd->child_count; i++) {
        fs_node* f = fs_cwd->children[i];
        if (!f->is_dir && strcmp(f->name, name) == 0) {
            fs_edit_mode = 1;
            fs_edit_file = f;
            fs_edit_pos = 0;
            vga_write("\n-- editing --\n");
            vga_write("TAB = save & leave\n");
            fs_edit_mode = 1;
            fs_edit_file = f;
            fs_edit_pos = 0;
            return;

        }
    }
    vga_write("file was not found.\n");
}
void fs_rdfile(const char* name) {
    for (int i = 0; i < fs_cwd->child_count; i++) {
        fs_node* f = fs_cwd->children[i];
        if (!f->is_dir && strcmp(f->name, name) == 0) {
            if (f->data)
                vga_write((char*)f->data);
            vga_write("\n");
            return;
        }
    }
    vga_write("file was not found.\n");
}


uint16_t vga_entry(char c, uint8_t color)
{
    return (uint16_t)c | (uint16_t)color << 8;
}

// Get default color based on SP8LF mode
static uint8_t vga_get_default_color(void) {
    if (sp8lf_mode) {
        return 0x70;  // SP8LF mode: black on white (fg=0x00, bg=0x07)
    }
    return 0x07;  // Normal mode: white on black (fg=0x07, bg=0x00)
}

void vga_clear(void)
{
    uint8_t default_color = vga_get_default_color();
    for (int y = 0; y < VGA_HEIGHT; y++)
        for (int x = 0; x < VGA_WIDTH; x++)
            vga_buffer[y * VGA_WIDTH + x] = vga_entry(' ', default_color);

    cursor_x = 0;
    cursor_y = 0;
    update_cursor();
}

static void vga_scroll(void)
{
    uint8_t default_color = vga_get_default_color();
    for (int y = 1; y < VGA_HEIGHT; y++)
        for (int x = 0; x < VGA_WIDTH; x++)
            vga_buffer[(y - 1) * VGA_WIDTH + x] =
                vga_buffer[y * VGA_WIDTH + x];

    for (int x = 0; x < VGA_WIDTH; x++)
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] =
            vga_entry(' ', default_color);

    cursor_y = VGA_HEIGHT - 1;
    update_cursor();
}

void vga_putc(char c)
{
    if (c == '\n')
    {
        cursor_x = 0;
        cursor_y++;
    }
    else
    {
        vga_buffer[cursor_y * VGA_WIDTH + cursor_x] =
            vga_entry(c, vga_color);
        cursor_x++;
    }

    if (cursor_x >= VGA_WIDTH)
    {
        cursor_x = 0;
        cursor_y++;
    }

    if (cursor_y >= VGA_HEIGHT)
        vga_scroll();
    
    update_cursor();
}

void vga_write(const char* str)
{
    while (*str)
        vga_putc(*str++);
}

void vga_set_color(uint8_t fg, uint8_t bg)
{
    vga_color = fg | bg << 4;
}

void testascii()
{
    vga_write("test ascii nie istnieje.\n");
}

// ---------- command handling ----------
// ---------- command handling ----------
void handle_command(const char* cmd)
{
    if (strncmp(cmd, "help", 5) == 0) {
        vga_write("\nexisting commands:\n");
        vga_write("help - list of all commands\ncalc <a> <operator> <b> - very easy and dumb calculator\n");
        vga_write("clear - clear screen\n");
        vga_write("about - about ibant-os\n");
        vga_write("version - show version\n");
        vga_write("halt - stop/halt CPU\n");
        vga_write("reboot - go back to _start();\nclick any key to continue...\n");
        get_char(); //wait for key input
        vga_write("bgcolor <0-15> - change background color\n");
        vga_write("fgcolor <0-15> - change foreground color\n");
        vga_write("echo <text> - echo your text!\n");
        vga_write("mkdir <dirname> - make new folder/directory\n");
        get_char(); //wait for key input
        vga_write("mkfile <filename> - make new file\nedfile <filename> - edit your files contents\nrdfile <filename> - read file contents\ndelfile <filename> - delete file\ndir - show all continuing directories in your current directory\ndir ~ - show all directories\ncd <directroy> - change directory\nls - list everything");
        get_char(); //wait for key input
    }
    else if (strncmp(cmd, "\n", 1) == 0){vga_write("");}
    else if (strncmp(cmd, "vgatest", 8) == 0) {testascii();}
    else if (strncmp(cmd, "edfile ", 7) == 0){
        vga_set_color(0x01, 0x07); //blue on white
        fs_edfile_start(cmd + 7);
        vga_set_color(0x00, 0x07); //default black on white
    }
    else if (strncmp(cmd, "cd ", 3) == 0){
        vga_set_color(0x0E, 0x07); //yellow on white
        fs_cd(cmd + 3);
        vga_set_color(0x01, 0x07); //blue on white
        }
    else if (strncmp(cmd, "ls", 2) == 0){
        vga_set_color(0x0E, 0x07); //yellow on white
        fs_ls();
        vga_set_color(0x01, 0x07); //blue on white
    }
    else if (strncmp(cmd, "mkfile ", 7) == 0) {
    const char* name = cmd + 7;
    vga_set_color(0x0E, 0x07); //yellow on black
    fs_mkfile(name);
    vga_write("\nMade new file: ");
    vga_write(name);
    vga_write(" in dir: ");
    vga_write(fs_cwd->name);
    vga_write("\n");
    vga_set_color(0x01, 0x07); //blue on white
    }
    else if (strncmp(cmd, "mkdir ", 6) == 0) {
        const char* name = cmd + 6;
        vga_set_color(0x0E, 0x07); //blue on white
        fs_mkdir(name);
        vga_write("\nMade new directory: ");
        vga_write(name);
        vga_write(" in dir: ");
        vga_write(fs_cwd->name);
        vga_write("\n");
        vga_set_color(0x01, 0x07); //blue on white
    }
    else if (strncmp(cmd, "rdfile ", 7) == 0) {
        vga_set_color(0x0E, 0x07); //default
        fs_rdfile(cmd + 7);
        vga_set_color(0x01, 0x07); //blue on white
    }
    else if (strncmp(cmd, "dir ~", 5) == 0) {
    vga_set_color(0x0E, 0x07); //blue on white
    fs_dir_from(fs_root);
    vga_set_color(0x01, 0x07); //blue on white
}
else if (strncmp(cmd, "dir", 3) == 0) {
    fs_dir_from(fs_cwd);
}
    else if (strncmp(cmd, "delfile ", 8) == 0) {
        fs_delfile(cmd + 8);
    }
    else if (strncmp(cmd, "clear", 6) == 0) {
        vga_clear();
    }
    else if (strncmp(cmd, "calc ", 5) == 0) {
        vga_set_color(0x0A, 0x00); //blue on black
        calc_command(cmd);
        vga_set_color(0x01, 0x00); //blue on black
        return;
    }
    else if (strncmp(cmd, "about", 6) == 0) {
        vga_set_color(0x04, 0x07);
        vga_write("\niBANT-OS: x86 aka: (i386) OS.\n");
        vga_write("Made by Julian Dziubak.\n");
        vga_write("Made in C\n");
        vga_set_color(0x01, 0x00); //blue on black
    }
    else if (strncmp(cmd, "version", 8) == 0) {
        vga_write("\niBANT-OS Version 1.6 ENGLISH\n");
    }
    else if (strncmp(cmd, "halt", 5) == 0) {
        while (1) { } // hang
    }
    else if (strncmp(cmd, "reboot", 7) == 0) {
        _start(); // restart
    }
    else if (strncmp(cmd, "bgcolor", 8) == 0) {
        int bg = atoi(cmd + 9); // skip "bgcolor "
        if (bg >= 0 && bg <= 15) vga_set_color(vga_color & 0x0F, bg);
        else vga_write("\nInvalid color. Use 0-15.\n");
    }
    else if (strncmp(cmd, "fgcolor", 8) == 0) {
        int fg = atoi(cmd + 9); // skip "fgcolor "
        if (fg >= 0 && fg <= 15) vga_set_color(fg, vga_color >> 4);
        else vga_write("\nInvalid color. Use 0-15.\n");
    }
    else if (strncmp(cmd, "echo", 5) == 0) {
        vga_write("\n");
        vga_write(cmd + 6); // skip "echo "
        vga_write("\n");
    }
    else {
        vga_write("\nUnknown command. Type help for commands.\n");
    }
}

// calculator
void calc_command(const char* cmd)
{
    int a = 0, b = 0;
    char op = 0;

    // skip "calc "
    cmd += 5;

    a = atoi(cmd);

    // move past first number
    while (*cmd >= '0' && *cmd <= '9') cmd++;

    // skip spaces
    while (*cmd == ' ') cmd++;

    op = *cmd++;

    while (*cmd == ' ') cmd++;

    b = atoi(cmd);

    int result = 0;
    int valid = 1;

    switch (op) {
        case '+': result = a + b; break;
        case '-': result = a - b; break;
        case '*': result = a * b; break;
        case '/':
            if (b == 0) {
                vga_write("division by zero\n");
                return;
            }
            result = a / b;
            break;
        default:
            valid = 0;
    }

    if (!valid) {
        vga_write("invalid operator\n");
        return;
    }

    char buf[32];
    int i = 30;
    buf[31] = 0;

    int r = result;
    if (r == 0) buf[i--] = '0';

    int neg = (r < 0);
    if (neg) r = -r;

    while (r > 0) {
        buf[i--] = '0' + (r % 10);
        r /= 10;
    }

    if (neg) buf[i--] = '-';

    vga_write("= ");
    vga_write(&buf[i + 1]);
    vga_write("\n");
}



// ---------- input handling ----------
void read_input_char(char c)
{
    if (fs_edit_mode) {
        if (c == '\t') { // TAB = zapis
            fs_edit_buffer[fs_edit_pos] = 0;

            fs_edit_file->data = kmalloc(fs_edit_pos + 1);
            memcpy(fs_edit_file->data, fs_edit_buffer, fs_edit_pos + 1);
            fs_edit_file->size = fs_edit_pos;

            fs_edit_mode = 0;
            fs_edit_file = 0;

            vga_write("\n-- SAVED --\n");
            vga_write("[ibant]> ");
            return;
        }

        if (c == '\b') { // Backspace in edit mode
            if (fs_edit_pos > 0) {
                fs_edit_pos--;
                // Move cursor back
                cursor_x--;
                // Erase character at cursor position
                vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = vga_entry(' ', vga_color);
                update_cursor();
            }
            return;
        }

        if (fs_edit_pos < sizeof(fs_edit_buffer) - 1) {
            fs_edit_buffer[fs_edit_pos++] = c;
            vga_putc(c);
        }
        return;
    }

    // NORMAL MODE
    if (c == '\n') {
        input_buffer[input_pos] = 0;
        handle_command(input_buffer);
        input_pos = 0;
        vga_write("\n");
    }
    else if (c == '\b') {
        if (input_pos > 0) {
            input_pos--;
            cursor_x--;
            vga_putc(' ');
            cursor_x--;
            update_cursor();
        }
    }
    else if (input_pos < MAX_CMD_LEN - 1) {
        input_buffer[input_pos++] = c;
        vga_putc(c);
    }
}


void grublmao(void) {
    vga_write("RUNNING WITH GRUB!!!\n now, returning to _start...\n");
}


// ---------- framebuffer for safe UI test ----------
static uint32_t *fb = 0;
static int fb_width = 0, fb_height = 0;

void init_framebuffer(uint32_t *framebuffer, int width, int height) {
    fb = framebuffer;
    fb_width = width;
    fb_height = height;
}

void putpixel_fb(int x, int y, uint32_t color) {
    if (!fb) return;
    if (x < 0 || x >= fb_width || y < 0 || y >= fb_height) return;
    fb[y * fb_width + x] = color;
}

void draw_test_fb(void) {
    if (!fb) return;
    for (int y = 0; y < fb_height; y++)
        for (int x = 0; x < fb_width; x++)
            putpixel_fb(x, y, (x + y) & 0xFFFFFF); // simple gradient
}

void bootimage() { // DO NOT TOUCH THIS AT ALL
    vga_clear();
    vga_set_color(0x00, 0x00); //blacl
    vga_clear();
    vga_write("\n");
    vga_write("\n");
    vga_write("\n");
    vga_write("\n");
    vga_write("\n");
    vga_write("                 \n"); //5 tabs
    vga_write("                 \n"); //5 tabs
    vga_set_color(0x04, 0x04); //red?
    vga_write("                 \n");
    vga_write("                 "); //5 tabs
    /*
    || |||||||
    || ||.   ||
    || |||||||
    || ||     ||
    || ||     ||
    || |||||||||
    */
   vga_set_color(0x0E, 0x04); //yellow on red
   vga_write("|| ");
   vga_set_color(0x07, 0x04); // white on red
   vga_write("|||||||        \n");
   vga_set_color(0x0E, 0x04); //yellow on red
   vga_write("|| ");
   vga_set_color(0x07, 0x04); // white on red
   vga_write("||    ||       \n");
   vga_set_color(0x0E, 0x04); //yellow on red
   vga_write("|| ");
   vga_set_color(0x07, 0x04); // white on red
   vga_write("|||||||        \n");
   vga_set_color(0x0E, 0x04); //yellow on red
   vga_write("|| ");
   vga_set_color(0x07, 0x04); // white on red
   vga_write("||     ||       \n");
   vga_set_color(0x0E, 0x04); //yellow on red
   vga_write("|| ");
   vga_set_color(0x07, 0x04); // white on red
   vga_write("||     ||       \n");
   vga_set_color(0x0E, 0x04); //yellow on red
   vga_write("|| ");
   vga_set_color(0x07, 0x04); // white on red
   vga_write("||||||||||      \n\n\n");
   vga_set_color(0x00, 0x07);
   vga_write("(c) iBANT-DEV - Julian Dziubak\n2025-2026\n\nBooting..");
   vga_set_color(0x07, 0x00);
}
// ---------- main loop ----------
void _start(void)
{
    grublmao();
    vga_clear();
    bootimage();
    // loop bootimage for 10 seconds, then do a black screen for 5 and then go return at the next below vga_clear
    delay_ms(100000);  // Display bootimage for 10 seconds
    vga_clear();      // Black screen
    delay_ms(50000);   // Wait 5 seconds
    vga_clear();      // Clear again before continuing
    fs_init();
    vga_set_color(0x07, 0x01); //white on blue
    vga_write("iBANT-OS 1.6 beta ENGLISH\n");
    vga_write("this is a unfished version of iBANT-OS so there may be errors. if you do find them, contact the creator (aka: me)");
    vga_set_color(0x0E, 0x04);
    vga_write("warning, if youre running this os please force PS/2 console!\n");
    vga_write("= = = welcome to iBANT-OS! = = =\n");
    vga_set_color(0x01, 0x00);
    vga_write("write 'help' for help!\n\n");

    // Start command prompt
    vga_write("[ibant]> ");

    while (1) {
        char c = get_char();
        read_input_char(c);
        if (c == '\n') {
            vga_write("[ibant]> ");
        }
    }
}
