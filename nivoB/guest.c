#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <inttypes.h>

// Sends a byte (8 bits) to a specified port
static void outb(uint16_t port, uint8_t value) {
    // Assembly instruction to send 'value' to 'port'
    asm("outb %0,%1" : /* no output */ : "a" (value), "Nd" (port) : "memory");
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
static char digits[] = "0123456789ABCDEF";

/**
 * Sends a character to a file descriptor by writing to the port 0xE9.
 *
 * @param fd File descriptor.
 * @param c Character to be sent.
 */
static void putc(int fd, char c) {
    if (fd == 1) { // Check if file descriptor is 1 (standard output)
        outb(0xE9, c); // Send character 'c' to port 0xE9
    }
}

/**
 * Converts an integer to a string representation in a specified base.
 *
 * @param xx The integer to convert.
 * @param base The base for conversion (e.g., 10 for decimal, 16 for hexadecimal).
 * @param buf Buffer to store the string representation.
 * @param len Pointer to store the length of the string.
 * @param sgn Indicates whether the number is signed.
 */
static void int_to_str(int xx, int base, char* buf, int* len, int sgn) {
    const char digits[] = "0123456789ABCDEF"; // Array of hexadecimal digit characters
    int i = 0; // Buffer index
    int neg = 0; // Negative flag
    uint32_t x; // Unsigned version of the number

    if (sgn && xx < 0) { // Check if the number is signed and negative
        neg = 1; // Set the negative flag
        x = -xx; // Convert to positive
    } else {
        x = xx; // Use the number as-is if it's non-negative
    }

    // Convert the number to a string in the specified base
    do {
        buf[i++] = digits[x % base]; // Convert the least significant digit to a character
    } while ((x /= base) != 0); // Repeat until all digits are processed

    if (neg) {
        buf[i++] = '-'; // Add the negative sign if necessary
    }

    *len = i; // Store the length of the string
}

/**
 * Prints an integer in the specified base to a file descriptor.
 *
 * @param fd File descriptor.
 * @param xx Integer to be printed.
 * @param base Number base (e.g., 10 for decimal, 16 for hexadecimal).
 * @param sgn Indicates whether the number is signed.
 */
static void printint(int fd, int xx, int base, int sgn) {
    char buf[16]; // Buffer to hold the number string
    int len; // Length of the string

    // Convert the integer to a string
    int_to_str(xx, base, buf, &len, sgn);

    // Output the characters in reverse order
    while (--len >= 0) {
        putc(fd, buf[len]);
    }
}

/**
 * Prints a pointer value in hexadecimal format to a file descriptor.
 *
 * @param fd File descriptor.
 * @param x Pointer value to be printed.
 */
static void printptr(int fd, uint64_t x) {
    int i;
    putc(fd, '0'); // Print '0'
    putc(fd, 'x'); // Print 'x' to indicate hexadecimal format
    for (i = 0; i < (sizeof(uint64_t) * 2); i++, x <<= 4) {
        // Print each nibble (4 bits) of the pointer value
        putc(fd, digits[x >> (sizeof(uint64_t) * 8 - 4)]);
    }
}

/**
 * Prints a formatted string to a file descriptor using a variable argument list.
 *
 * @param fd File descriptor.
 * @param fmt Format string.
 * @param ap Variable argument list.
 */
void vprintf(int fd, const char *fmt, va_list ap) {
    char *s; // Pointer for strings
    int c, i, state = 0; // 'c' is the current character, 'i' is the loop index, 'state' tracks format state

    // Process each character in the format string
    for (i = 0; fmt[i]; i++) {
        c = fmt[i] & 0xff; // Get the current character
        if (state == 0) {
            if (c == '%') {
                state = '%'; // Enter format specifier state
            } else {
                putc(fd, c); // Print regular characters
            }
        } else if (state == '%') {
            switch (c) {
                case 'd':
                    printint(fd, va_arg(ap, int), 10, 1); // Print signed decimal integer
                    break;
                case 'x':
                    printint(fd, va_arg(ap, int), 16, 0); // Print unsigned hexadecimal integer
                    break;
                case 'p':
                    printptr(fd, va_arg(ap, uint64_t)); // Print pointer
                    break;
                case 's':
                    s = va_arg(ap, char*); // Get string argument
                    if (!s) s = "(null)"; // Handle null strings
                    while (*s) {
                        putc(fd, *s++); // Print each character in the string
                    }
                    break;
                case 'c':
                    putc(fd, va_arg(ap, int)); // Print character
                    break;
                case '%':
                    putc(fd, '%'); // Print '%' character
                    break;
                default:
                    putc(fd, '%'); // Print unknown format specifier as is
                    putc(fd, c);
                    break;
            }
            state = 0; // Reset state after processing format specifier
        }
    }
}

/**
 * Prints a formatted string to a file descriptor using a variable argument list.
 *
 * @param fd File descriptor.
 * @param fmt Format string.
 * @param ... Variable arguments.
 */
void fprintf(int fd, const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt); // Initialize the variable argument list
    vprintf(fd, fmt, ap); // Call vprintf to handle the formatted output
    va_end(ap); // Clean up the variable argument list
}

/**
 * Prints a formatted string using a variable argument list.
 *
 * @param fmt Format string.
 * @param ... Variable arguments.
 */
void printf(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt); // Initialize the variable argument list
    vprintf(1, fmt, ap); // Call vprintf to handle the formatted output to standard output
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
        if (c < '0' || c > '9') continue; // Skip non-digit characters
        num = num * 10 + (c - '0'); // Shift the current number left by one decimal place and add the new digit
    }

    return num; // Return the constructed integer
}

/**
 * Computes the factorial of a number iteratively.
 *
 * @param n The number to compute the factorial of.
 * @return The factorial of the number.
 */
int factorial(int n) {
    int result = 1; // Initialize result to 1
    for (int i = 2; i <= n; i++) {
        result *= i; // Multiply result by each integer from 2 to n
    }
    return result; // Return the computed factorial
}

/**
 * Checks if a number is prime.
 *
 * @param n The number to check for primality.
 * @return 1 if the number is prime, 0 otherwise.
 */
int is_prime(int n) {
    if (n <= 1) return 0; // Numbers less than or equal to 1 are not prime
    if (n <= 3) return 1; // 2 and 3 are prime
    if (n % 2 == 0 || n % 3 == 0) return 0; // Divisible by 2 or 3
    for (int i = 5; i * i <= n; i += 6) {
        if (n % i == 0 || n % (i + 2) == 0) return 0; // Divisible by any number in the form of 6k ± 1
    }
    return 1; // The number is prime
}

/**
 * Entry point of the program. Initializes the CPU state, computes factorial and primality tests, and prints the results.
 */
void __attribute__((noreturn)) __attribute__((section(".start"))) _start(void) {
    // Print the factorial of 5
    printf("Factorial of 5 is %d\n", factorial(5));

    // Check if 11 is a prime number and print the result
    printf("Is 11 prime? %d\n", is_prime(11));

    // Check if 15 is a prime number and print the result
    printf("Is 15 prime? %d\n", is_prime(15));

    // Halts the CPU indefinitely
    for (;;) {
        asm volatile("hlt");
    }
}
