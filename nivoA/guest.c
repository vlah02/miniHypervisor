#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <inttypes.h>

// Sends a byte (8 bits) to a specified port
static void outb(uint16_t port, uint8_t value) {
    // Assembly instruction to send 'value' to 'port'
    asm("outb %0,%1" : /* no output */ : "a" (value), "Nd" (port) : "memory");
}

// Sends a 32-bit value to a specified port
static void out(uint16_t port, uint32_t value) {
    // Assembly instruction to send 'value' to 'port'
    asm("out %0, %1" : /* no output */ : "a"(value), "Nd" (port) : "memory");
}

// Receives a 32-bit value from a specified port
static int in(uint16_t port) {
    int ret;
    // Assembly instruction to receive a 32-bit value from 'port' into 'ret'
    asm("in %1, %0" : "=a"(ret) : "Nd"(port));
    return ret; // Return the received value
}

// Receives a byte (8 bits) from a specified port
static char inb(uint16_t port) {
    char ret;
    // Assembly instruction to receive a byte from 'port' into 'ret'
    asm("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret; // Return the received byte
}

// Receives a character from the port 0xE9
static char getchar() {
    // Use inb to get a character from port 0xE9
    return inb(0xE9);
}

// Array of hexadecimal digit characters
static const char digits[] = "0123456789ABCDEF";

/**
 * Sends a character to port 0xE9.
 *
 * @param c Character to be sent.
 */
static void putc(char c) {
    // Send character 'c' to port 0xE9
    outb(0xE9, c);
}

/**
 * Prints an integer in the specified base to port 0xE9.
 *
 * @param xx Integer to be printed.
 * @param base Number base (e.g., 10 for decimal, 16 for hexadecimal).
 * @param sgn Indicates whether the number is signed.
 */
static void printint(int xx, int base, int sgn) {
    char buf[16]; // Buffer to hold the number string
    int i, neg; // 'i' is the buffer index, 'neg' is the negative flag
    uint32_t x; // Unsigned version of the number

    neg = 0; // Assume the number is non-negative initially
    if (sgn && xx < 0) { // Check if the number is signed and negative
        neg = 1; // Set the negative flag
        x = -xx; // Convert to positive
    } else {
        x = xx; // Use the number as-is if it's non-negative
    }

    i = 0; // Initialize buffer index
    do {
        buf[i++] = digits[x % base]; // Convert the least significant digit to a character
    } while ((x /= base) != 0); // Repeat until all digits are processed

    if (neg) {
        buf[i++] = '-'; // Add the negative sign if necessary
    }

    while (--i >= 0) {
        putc(buf[i]); // Output the characters in reverse order
    }
}

/**
 * Prints a pointer value in hexadecimal format to port 0xE9.
 *
 * @param x Pointer value to be printed.
 */
static void printptr(uint64_t x) {
    putc('0'); // Print '0'
    putc('x'); // Print 'x' to indicate hexadecimal format
    for (int i = 0; i < (sizeof(uint64_t) * 2); i++, x <<= 4) {
        // Print each nibble (4 bits) of the pointer value
        putc(digits[x >> (sizeof(uint64_t) * 8 - 4)]);
    }
}

/**
 * Prints a formatted string to port 0xE9 using a variable argument list.
 *
 * @param fmt Format string.
 * @param ap Variable argument list.
 */
void vprintf(const char *fmt, va_list ap) {
    char *s; // Pointer for strings
    int c, i, state; // 'c' is the current character, 'i' is the loop index, 'state' tracks format state

    state = 0; // Initial state (no format specifier)
    for (i = 0; fmt[i]; i++) {
        c = fmt[i] & 0xff; // Get the current character
        if (state == 0) {
            if (c == '%') {
                state = '%'; // Enter format specifier state
            } else {
                putc(c); // Print regular characters
            }
        } else if (state == '%') {
            if (c == 'd') {
                printint(va_arg(ap, int), 10, 1); // Print signed decimal integer
            } else if (c == 'l') {
                printint(va_arg(ap, uint64_t), 10, 0); // Print unsigned long integer
            } else if (c == 'x') {
                printint(va_arg(ap, int), 16, 0); // Print unsigned hexadecimal integer
            } else if (c == 'p') {
                printptr(va_arg(ap, uint64_t)); // Print pointer
            } else if (c == 's') {
                s = va_arg(ap, char*); // Get string argument
                if (s == 0) {
                    s = "(null)"; // Handle null strings
                }
                while (*s != 0) {
                    putc(*s); // Print each character in the string
                    s++;
                }
            } else if (c == 'c') {
                putc(va_arg(ap, uint32_t)); // Print character
            } else if (c == '%') {
                putc(c); // Print '%' character
            } else {
                putc('%'); // Print unknown format specifier as is
                putc(c);
            }
            state = 0; // Reset state after processing format specifier
        }
    }
}

/**
 * Prints a formatted string to port 0xE9.
 *
 * @param fmt Format string.
 * @param ... Variable arguments.
 */
void printf(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt); // Initialize the variable argument list
    vprintf(fmt, ap); // Call vprintf to handle the formatted output
    va_end(ap); // Clean up the variable argument list
}

/**
 * Scans an integer from the input received from the port 0xE9.
 *
 * @return The scanned integer.
 */
int scan_int() {
    char c; // Character read from input
    int num = 0; // The integer being constructed

    // Read characters until a newline is encountered
    while ((c = getchar()) != '\n') {
        num *= 10; // Shift the current number left by one decimal place
        num += c - '0'; // Add the new digit to the number
    }

    return num; // Return the constructed integer
}

/**
 * Entry point of the program. Initializes the CPU state, scans two integers from input, computes their sum, and prints the result.
 */
void __attribute__((noreturn)) __attribute__((section(".start"))) _start(void) {
    int a, b;

    // Prompt the user to enter the value of 'a'
    printf("Enter a: ");
    a = scan_int(); // Scan the value of 'a'

    // Prompt the user to enter the value of 'b'
    printf("Enter b: ");
    b = scan_int(); // Scan the value of 'b'

    // Compute the sum of 'a' and 'b' and print the result
    printf("%d + %d = %d\n", a, b, (a + b));

    // Halts the CPU indefinitely
    for (;;) {
        asm("hlt");
    }
}
