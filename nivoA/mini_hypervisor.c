#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/kvm.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <getopt.h>
#include <pty.h>
#include <semaphore.h>

// Define bitmasks for page directory and table entries
#define PDE64_PRESENT 1
#define PDE64_RW (1U << 1)
#define PDE64_USER (1U << 2)
#define PDE64_PS (1U << 7)

#define CR4_PAE (1U << 5)
#define CR0_PE 1u
#define CR0_PG (1U << 31)
#define EFER_LME (1U << 8)
#define EFER_LMA (1U << 10)

#define SIZE2MB (2 * 1024 * 1024)

// Enum for page size (2MB or 4KB)
enum PageSize { MB2, KB4 };

// Structure representing a hypervisor, containing the KVM file descriptor and the KVM run mmap size
struct hypervisor {
    int kvm_fd; // File descriptor for /dev/kvm
    int kvm_run_mmap_size; // Size of the memory map for the KVM run structure
};

/**
 * Initializes the hypervisor by opening the /dev/kvm file and getting the KVM run mmap size.
 *
 * @param hypervisor Pointer to the hypervisor structure.
 * @return 0 on success, -1 on failure.
 */
int init_hypervisor(struct hypervisor* hypervisor) {
    // Open the /dev/kvm file
    hypervisor->kvm_fd = open("/dev/kvm", O_RDWR);
    if (hypervisor->kvm_fd < 0) {
        // Print an error message if the file cannot be opened
        perror("ERROR: Unable to open /dev/kvm file");
        return -1;
    }

    // Get the size of the memory map for the KVM run structure
    hypervisor->kvm_run_mmap_size = ioctl(hypervisor->kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
    if (hypervisor->kvm_run_mmap_size < 0) {
        // Print an error message if the ioctl call fails
        perror("ERROR: Failed ioctl KVM_GET_VCPU_MMAP_SIZE");
        fprintf(stderr, "KVM_GET_VCPU_MMAP_SIZE: %s\n", strerror(errno));
        close(hypervisor->kvm_fd);
        return -1;
    }

    return 0;
}

// Structure representing a guest VM
struct guest {
    int vm_fd; // File descriptor for the VM
    int vm_vcpu; // File descriptor for the virtual CPU
    char* mem; // Pointer to the memory allocated for the guest
    struct kvm_run* kvm_run; // Pointer to the KVM run structure
};

/**
 * Creates a guest VM by issuing an ioctl call to KVM_CREATE_VM.
 *
 * @param hypervisor Pointer to the hypervisor structure.
 * @param vm Pointer to the guest structure.
 * @return 0 on success, -1 on failure.
 */
int create_guest(struct hypervisor* hypervisor, struct guest* vm) {
    // Create a VM by issuing an ioctl call to KVM_CREATE_VM
    vm->vm_fd = ioctl(hypervisor->kvm_fd, KVM_CREATE_VM, 0);
    if (vm->vm_fd < 0) {
        // Print an error message if the VM creation fails
        perror("ERROR: Failed to create KVM VM");
        fprintf(stderr, "KVM_CREATE_VM: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

/**
 * Allocates memory for the guest VM and sets up the user memory region by issuing an ioctl call to KVM_SET_USER_MEMORY_REGION.
 *
 * @param vm Pointer to the guest structure.
 * @param mem_size Size of the memory to be allocated.
 * @return 0 on success, -1 on failure.
 */
int create_memory_region(struct guest* vm, size_t mem_size) {
    struct kvm_userspace_memory_region region;

    // Allocate memory for the guest VM
    vm->mem = mmap(NULL, mem_size, PROT_EXEC | PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (vm->mem == MAP_FAILED) {
        // Print an error message if memory allocation fails
        perror("ERROR: Failed to mmap memory for guest");
        return -1;
    }

    // Set up the memory region structure
    region.slot = 0;
    region.flags = 0;
    region.guest_phys_addr = 0;
    region.memory_size = mem_size;
    region.userspace_addr = (unsigned long)vm->mem;

    // Set the user memory region for the guest VM
    if (ioctl(vm->vm_fd, KVM_SET_USER_MEMORY_REGION, &region) < 0) {
        // Print an error message if the ioctl call fails
        perror("ERROR: Failed ioctl KVM_SET_USER_MEMORY_REGION");
        fprintf(stderr, "KVM_SET_USER_MEMORY_REGION: %s\n", strerror(errno));
        munmap(vm->mem, mem_size);
        return -1;
    }

    return 0;
}

/**
 * Creates a virtual CPU (vCPU) for the guest VM by issuing an ioctl call to KVM_CREATE_VCPU.
 *
 * @param vm Pointer to the guest structure.
 * @return 0 on success, -1 on failure.
 */
int create_vcpu(struct guest* vm) {
    // Create a virtual CPU by issuing an ioctl call to KVM_CREATE_VCPU
    vm->vm_vcpu = ioctl(vm->vm_fd, KVM_CREATE_VCPU, 0);
    if (vm->vm_vcpu < 0) {
        // Print an error message if the virtual CPU creation fails
        perror("ERROR: Failed ioctl KVM_CREATE_VCPU");
        fprintf(stderr, "KVM_CREATE_VCPU: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

/**
 * Maps the KVM run structure into the process's address space by issuing an mmap call.
 *
 * @param hypervisor Pointer to the hypervisor structure.
 * @param vm Pointer to the guest structure.
 * @return 0 on success, -1 on failure.
 */
int create_kvm_run(struct hypervisor* hypervisor, struct guest* vm) {
    // Map the KVM run structure into the process's address space
    vm->kvm_run = mmap(NULL, hypervisor->kvm_run_mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, vm->vm_vcpu, 0);
    if (vm->kvm_run == MAP_FAILED) {
        // Print an error message if the mmap call fails
        perror("ERROR: Failed to mmap KVM run structure");
        return -1;
    }

    return 0;
}

/**
 * Sets up a 64-bit code segment for the guest VM by configuring the segment descriptor.
 *
 * @param sregs Pointer to the special registers structure.
 */
void setup_64bit_code_segment(struct kvm_sregs* sregs) {
    struct kvm_segment segment = {
        .base = 0,
        .limit = 0xffffffff,
        .present = 1,
        .type = 11, // Execute/Read, accessed
        .dpl = 0,
        .db = 0,
        .s = 1,
        .l = 1, // 64-bit code segment
        .g = 1
    };

    // Set the code segment
    sregs->cs = segment;
    segment.type = 3; // Read/Write, accessed
    // Set the data segments
    sregs->ds = sregs->es = sregs->fs = sregs->gs = sregs->ss = segment;
}

/**
 * Sets up long mode for the guest VM by configuring the paging structures and special registers.
 *
 * @param vm Pointer to the guest structure.
 * @param mem_size Size of the memory allocated for the guest.
 * @param page_size Page size (2MB or 4KB).
 * @return Address of the first page on success, -1 on failure.
 */
int setup_long_mode(struct guest* vm, size_t mem_size, enum PageSize page_size) {
    struct kvm_sregs sregs;

    // Get the special registers of the virtual CPU
    if (ioctl(vm->vm_vcpu, KVM_GET_SREGS, &sregs) < 0) {
        // Print an error message if the ioctl call fails
        perror("ERROR: Failed ioctl KVM_GET_SREGS");
        fprintf(stderr, "KVM_GET_SREGS: %s\n", strerror(errno));
        return -1;
    }

    uint64_t pml4_addr = 0; // Address of the PML4
    uint64_t* pml4 = (void*)(vm->mem + pml4_addr); // Pointer to the PML4

    uint64_t pdpt_addr = 0x1000; // Address of the PDPT
    uint64_t* pdpt = (void*)(vm->mem + pdpt_addr); // Pointer to the PDPT

    uint64_t pd_addr = 0x2000; // Address of the page directory
    uint64_t* pd = (void*)(vm->mem + pd_addr); // Pointer to the page directory

    uint64_t page = 0x3000; // Initial page address

    // Set up the PML4 entry
    pml4[0] = PDE64_PRESENT | PDE64_RW | PDE64_USER | pdpt_addr;
    // Set up the PDPT entry
    pdpt[0] = PDE64_PRESENT | PDE64_RW | PDE64_USER | pd_addr;

    if (page_size == MB2) {
        // Align the page address to 2MB
        page = (page / SIZE2MB + 1) * SIZE2MB;
        uint64_t page_address = page;
        for (int i = 0; i < mem_size / SIZE2MB - 1; i++) {
            // Set up the page directory entry for 2MB pages
            pd[i] = PDE64_PRESENT | PDE64_RW | PDE64_USER | PDE64_PS | page_address;
            page_address += SIZE2MB;
        }
    } else {
        // Set up the page directory entry for 4KB pages
        for (int i = 0; i < mem_size / SIZE2MB; i++) {
            pd[i] = PDE64_PRESENT | PDE64_RW | PDE64_USER | page;
            page += 0x1000;
        }

        uint64_t page_address = page;
        for (int i = 0; i < mem_size / SIZE2MB; i++) {
            uint64_t pt_addr = pd[i] & ~0xFFFUL; // Address of the page table
            uint64_t* pt = (void*)vm->mem + pt_addr; // Pointer to the page table

            for (int j = 0; j < 512; j++) {
                if (page_address > mem_size) break;
                // Set up the page table entry
                pt[j] = page_address | PDE64_PRESENT | PDE64_RW | PDE64_USER;
                page_address += 0x1000;
            }
        }
    }

    // Set the special registers
    sregs.cr3 = pml4_addr; // Set the CR3 register to the address of the PML4
    sregs.cr4 = CR4_PAE; // Enable PAE
    sregs.cr0 = CR0_PE | CR0_PG; // Enable protected mode and paging
    sregs.efer = EFER_LMA | EFER_LME; // Enable long mode

    // Set up the 64-bit code segment
    setup_64bit_code_segment(&sregs);

    // Set the special registers for the virtual CPU
    if (ioctl(vm->vm_vcpu, KVM_SET_SREGS, &sregs) < 0) {
        // Print an error message if the ioctl call fails
        perror("ERROR: Failed ioctl KVM_SET_SREGS");
        fprintf(stderr, "KVM_SET_SREGS: %s\n", strerror(errno));
        return -1;
    }

    return page;
}

/**
 * Sets up the general-purpose registers for the guest VM by issuing ioctl calls to KVM_GET_REGS and KVM_SET_REGS.
 *
 * @param vm Pointer to the guest structure.
 * @return 0 on success, -1 on failure.
 */
int setup_registers(struct guest* vm) {
    struct kvm_regs regs;

    // Get the general-purpose registers of the virtual CPU
    if (ioctl(vm->vm_vcpu, KVM_GET_REGS, &regs) < 0) {
        // Print an error message if the ioctl call fails
        perror("ERROR: Failed ioctl KVM_GET_REGS");
        fprintf(stderr, "KVM_GET_REGS %s\n", strerror(errno));
        return -1;
    }

    // Clear the registers
    memset(&regs, 0, sizeof(regs));

    regs.rflags = 2; // Set the RFLAGS register
    regs.rip = 0; // Set the instruction pointer to 0
    regs.rsp = 1 << 21; // Set the stack pointer

    // Set the general-purpose registers for the virtual CPU
    if (ioctl(vm->vm_vcpu, KVM_SET_REGS, &regs) < 0) {
        // Print an error message if the ioctl call fails
        perror("ERROR: Failed ioctl KVM_SET_REGS");
        fprintf(stderr, "KVM_SET_REGS %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

/**
 * Handles IO operations for the guest VM, specifically for port 0xE9.
 *
 * @param vm Pointer to the guest structure.
 * @return 0 on success, -1 on failure.
 */
int handle_io(struct guest* vm) {
    // Check if the IO port is 0xE9
    if (vm->kvm_run->io.port == 0xE9) {
        // Check if the direction is out
        if (vm->kvm_run->io.direction == KVM_EXIT_IO_OUT) {
            char c = *((char*)vm->kvm_run + vm->kvm_run->io.data_offset);
            putchar(c); // Print the character
            fflush(stdout);
        } else if (vm->kvm_run->io.direction == KVM_EXIT_IO_IN) {
            char c = getchar(); // Get a character from the user
            *((char*)vm->kvm_run + vm->kvm_run->io.data_offset) = c;
        } else {
            fprintf(stderr, "ERROR: Unsupported IO operation on port 0xE9\n");
            return -1;
        }
    } else {
        fprintf(stderr, "ERROR: Unsupported IO port 0x%x\n", vm->kvm_run->io.port);
        return -1;
    }
    return 0;
}

/**
 * Handles the HALT exit reason by printing a message.
 *
 * @param vm Pointer to the guest structure.
 * @return 1 to indicate the VM should stop running.
 */
int exit_halt(struct guest* vm) {
    printf("KVM_EXIT_HLT\n");
    return 1;
}

// Typedef for the exit handlers
typedef int (*Handler)(struct guest* vm);

// Array of exit handlers
static Handler handlers[] = {
    NULL, NULL, &handle_io, NULL, NULL, &exit_halt,
    NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL,
    NULL
};

/**
 * Runs the guest VM in a loop, handling exit reasons using the handlers array.
 *
 * @param par Pointer to the guest structure.
 * @return NULL on completion.
 */
void* run_guest(void* par) {
    struct guest* vm = (struct guest*)par;
    int stop = 0;
    int ret;

    while (stop == 0) {
        // Run the virtual CPU
        ret = ioctl(vm->vm_vcpu, KVM_RUN, 0);
        if (ret < 0) {
            // Print an error message if the ioctl call fails
            perror("ERROR: Failed ioctl KVM_RUN");
            fprintf(stderr, "KVM_RUN: %s\n", strerror(errno));
            return NULL;
        }

        int exit_reason = vm->kvm_run->exit_reason; // Get the exit reason

        // Call the appropriate handler for the exit reason
        if (handlers[exit_reason]) {
            stop = handlers[exit_reason](vm);
        } else {
            printf("Unknown exit reason %d\n", exit_reason);
            stop = -1;
        }
    }

    return NULL;
}

/**
 * Initializes the guest VM by creating the guest, memory region, vCPU, and setting up long mode and registers.
 * Loads the guest image into memory.
 *
 * @param hypervisor Pointer to the hypervisor structure.
 * @param vm Pointer to the guest structure.
 * @param mem_size Size of the memory allocated for the guest.
 * @param page_size Page size (2MB or 4KB).
 * @param img Pointer to the guest image file.
 * @return Starting address of the loaded image on success, -1 on failure.
 */
int init_guest(struct hypervisor* hypervisor, struct guest* vm, size_t mem_size, enum PageSize page_size, FILE* img) {
    int starting_address;

    if (create_guest(hypervisor, vm) < 0) return -1;
    if (create_memory_region(vm, mem_size) < 0) return -1;
    if (create_vcpu(vm) < 0) return -1;
    if (create_kvm_run(hypervisor, vm) < 0) return -1;
    if ((starting_address = setup_long_mode(vm, mem_size, page_size)) < 0) return -1;
    if (setup_registers(vm) < 0) return -1;

    // Load the guest image into memory
    char* p = vm->mem + starting_address;
    while (!feof(img)) {
        int r = fread(p, 1, 1024, img);
        p += r;
    }

    return starting_address;
}

/**
 * Main function to parse command line arguments, initialize the hypervisor and guest VM, and start running the guest.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, exits with EXIT_FAILURE on failure.
 */
int main(int argc, char* argv[]) {
    int opt;
    int memory = 0; // Memory size in bytes
    enum PageSize page_size; // Page size
    struct hypervisor hypervisor;
    FILE* img = NULL; // File pointer for the guest image
    char* guest_file = NULL; // Path to the guest image file

    // Define the command line options
    struct option long_options[] = {
        {"memory", required_argument, 0, 'm'},
        {"page", required_argument, 0, 'p'},
        {"guest", required_argument, 0, 'g'},
        {0, 0, 0, 0,}
    };

    // Parse the command line options
    while ((opt = getopt_long(argc, argv, "m:p:g:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'm':
                memory = atoi(optarg) * 1024 * 1024; // Convert memory size to bytes
                break;
            case 'p':
                page_size = (atoi(optarg) == 4) ? KB4 : MB2; // Set the page size
                break;
            case 'g':
                guest_file = optarg; // Set the guest image file
                break;
            default:
                fprintf(stderr, "Usage: %s --memory <MB> --page <4|2> --guest <guest.img>\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // Check if the guest file was provided
    if (guest_file == NULL) {
        fprintf(stderr, "ERROR: Guest file not provided.\n");
        exit(EXIT_FAILURE);
    }

    // Initialize the hypervisor
    if (init_hypervisor(&hypervisor) < 0) {
        printf("ERROR: Unable to initialize hypervisor\n");
        exit(EXIT_FAILURE);
    }

    // Open the guest image file
    img = fopen(guest_file, "r");
    if (img == NULL) {
        printf("ERROR: Unable to open file %s\n", guest_file);
        exit(EXIT_FAILURE);
    }

    struct guest vm; // Structure representing the guest VM

    // Initialize the guest VM
    if (init_guest(&hypervisor, &vm, memory, page_size, img) < 0) {
        printf("ERROR: Unable to initialize guest\n");
        fclose(img);
        exit(EXIT_FAILURE);
    }

    // Run the guest VM
    run_guest(&vm);

    // Close the guest image file
    fclose(img);
    return 0;
}
