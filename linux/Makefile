CONFIG_MODULE_SIG=n
CONFIG_MODULE_FORCE_UNLOAD=yes

obj-m += adafruit_seesaw.o

PWD := $(CURDIR)

all: module dt

module: preprocess
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

preprocess: adafruit_seesaw.c
	sed -i 's/\.probe/\.probe_new/g' adafruit_seesaw.c

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

dt: test_overlay.dts
	dtc -@ -I dts -o dtb -o test_overlay.dtbo test_overlay.dts
