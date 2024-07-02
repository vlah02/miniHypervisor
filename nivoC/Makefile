all: run

run: mini_hypervisor guest.img
	sudo ./mini_hypervisor -m 4 -p 2 -g guest1.img guest2.img guest3.img --file ./primer.txt

mini_hypervisor: mini_hypervisor.c
	gcc $^ -o $@ -pthread -g -lutil

guest.img: guest.o
	ld -T guest.ld guest.o -o guest1.img
	ld -T guest.ld guest.o -o guest2.img
	ld -T guest.ld guest.o -o guest3.img

guest.o: guest.c
	$(CC) -m64 -ffreestanding -fno-pic -c -o $@ $^

clean:
	rm -f mini_hypervisor guest.o guest*.img vm_*.txt