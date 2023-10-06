## TODO
- [x] Implement Arduino driver
- [x] Get linux driver working on kernel v6.1
- [x] Driver test on latest kernel
- [x] Fix formatting for defines
- [ ] Add documentation at Documentation/devicetree/bindings/adafruit_seesaw.yaml
- [ ] Add tristate config option to Kconfig
- [ ] Add build target to Makefile

## Notes:
- Split the patch in two, the first one would include compatibles and the second one with the driver code
  > Typically a binding patch should be first in the series (i.e. [1/2]) with the driver next (i.e. [2/2]) so that checkpatch does not signal a warning about an undocumented compatible string.
- What does "rebase patch for device tree before code" mean?
  > Source: https://lore.kernel.org/linux-input/20210603221801.16586-1-oleg@kaa.org.ua/
