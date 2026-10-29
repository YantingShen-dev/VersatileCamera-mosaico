# Composition root

`main.c` initializes NVS, starts the retained Iris recovery integration, opens the GSP bundle, initializes display/touch, and connects the UI controller to the vision worker. UI and model state stay private to their owning components. The running instance lives until reboot.
