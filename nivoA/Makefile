all: run

run: mini_hypervisor guest.img
	sudo ./mini_hypervisor --memory 4 --page 2 --guest guest.img

mini_hypervisor: mini_hypervisor.c
	gcc mini_hypervisor.c -o mini_hypervisor

guest.img: guest.o
	ld -T guest.ld guest.o -o guest.img

guest.o: guest.c
	$(CC) -m64 -ffreestanding -fno-pic -c -o $@ $^

clean:
	rm -f mini_hypervisor guest.o guest.img