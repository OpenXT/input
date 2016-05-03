The input daemon captures all input from /dev/input/event* and sends it to the right VM.
Input events are sent either through qemu for emulated input, or directly to a pv frontend (xenmou) if the VM has one.
Alternatively, input events can be handled by a plugin.

The input daemon is responsible for transforming relative mouse events to absolute "tablet" events when required.
When the guest VM has OpenXT tools installed (xenmou(2) and the resolution agent), using absolute events and knowing the guest resolution allows the input daemon to precisely determine where the cursor is on screen.
When the input daemon detects a "strong" move to the edge of the screen, it can perform "seamless switching", according to the settings and display layout defined in the UI.

The input daemon can also handle multitouch (digitizer) events, and either send them as such to a xenmou2 frontend, or "demultitouch" and send them as basic absolute tablet events.
Note: a "tablet" here is a single point absolute input device, like graphics tablets. The touchscreen of tablet computers is referred to as a "digitizer".
