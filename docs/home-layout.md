# Home Layout

The home screen is a fixed firmware-owned layout. Its internal model contains
three ordered destinations: Notes, Time, and Settings. The UI renders those
destinations as equal full-width sections and routes touch input directly to the
matching state.

`pj_home_layout_defaults()` constructs the model during UI initialization. The
layout types remain internal so native tests and the simulator exercise the same
navigation model as firmware, but the device does not persist a user-supplied
layout and does not expose home-layout mutation over LAN or USB-C.

Changes to the home layout are product firmware changes. Update the compiled
default and its UI tests together, then validate the resulting firmware-backed
simulator gallery before flashing hardware.
