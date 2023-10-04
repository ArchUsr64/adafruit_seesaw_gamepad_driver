CONFIG_MODULE_SIG=n
CONFIG_MODULE_FORCE_UNLOAD=yes

obj-m += adafruit-seesaw.o

PWD := $(CURDIR)

all: module dt

module:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

dt: test_overlay.dts
	dtc -@ -I dts -o dtb -o test_overlay.dtbo test_overlay.dts
