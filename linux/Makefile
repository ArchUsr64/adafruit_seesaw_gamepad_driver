CONFIG_MODULE_SIG=n
CONFIG_MODULE_FORCE_UNLOAD=yes

obj-m += adafruit-seesaw.o

PWD := $(CURDIR)

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

