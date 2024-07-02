all: run

run: mini_hypervisor guest.img
	sudo ./mini_hypervisor --memory 4 --page 2 --guest guest1.img guest2.img guest3.img

mini_hypervisor: mini_hypervisor.c
	gcc $^ -o $@ -pthread -g -lutil

guest.img: guest.o
	ld -T guest.ld guest.o -o guest1.img
	ld -T guest.ld guest.o -o guest2.img
	ld -T guest.ld guest.o -o guest3.img

guest.o: guest.c
	$(CC) -m64 -ffreestanding -fno-pic -c -o $@ $^

clean:
	rm -f mini_hypervisor guest.o guest*.img