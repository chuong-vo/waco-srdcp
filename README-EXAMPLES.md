Contiki Examples
================

The examples/ directory contains a few examples that will help you get
started with Contiki.

To run the example programs, you need either to be running Linux or FreeBSD (or
any other UNIX-type system), or install Cygwin if you are running Microsoft
Windows [http://cygwin.com](http://cygwin.com). As a minimum you will need to
have the gcc C compiler installed. To run the examples in the 'netsim' target,
you need to have GTK 1.x development libraries installed. These are usually
called 'gtk-devel', 'libgtk1-devel' or similar in your Linux software
installation programs.

compile-platforms/
------------------

A test script that compiles Contiki for a number of platforms and reports any
errors found during the build.

email/
------

An email program supporting SMTP. It can be compiled and run in the 'win32'
target by typing the following commands:

    cd examples/email
    make
    ./email-client.win32

Most likely you'll have to adjust the TCP/IP values set in main() in
platform/win32/contiki-main.c to match your needs.

Please consult cpu/native/net/README-WPCAP.md as well.

esb/
----

A set of demo applications for the ESB board.

ftp/
----

An FTP client supporting download. It can be compiled and run in the 'win32'
target by typing the following commands:

    cd examples/ftp
    make
    ./ftp-client.win32

Most likely you'll have to adjust the TCP/IP values set in main() in
platform/win32/contiki-main.c to match your needs.

Please consult cpu/native/net/README-WPCAP.md as well.

hello-world/
------------

A really simple Contiki program that shows how to write Contiki programs. To
compile and test the program, go into the hello-world directory:

    cd examples/hello-world

Run the 'make' command.

    make

This will compile the hello-world program in the 'native' target.  This causes
the entire Contiki operating system and the hello-world application to be
compiled into a single program that can be run by typing the following command:

    ./hello-world.native

This will print out the following text:

    Contiki initiated, now starting process scheduling
    Hello, world

The program will then appear to hang, and must be stopped by pressing the C key
while holding down the Control key.

irc/
----

An IRC client. It can be compiled and run in the 'win32' target by
typing the following commands:

    cd examples/irc
    make
    ./irc-client.win32

Most likely you'll have to adjust the TCP/IP values set in main() in
platform/win32/contiki-main.c to match your needs.

Please consult cpu/native/net/README-WPCAP.md as well.

multi-threading/
----------------

A quite simple demonstration of the Contiki multi-threading library
employing two worker threads each running a recursive function. It
can be compiled and run in the 'native' target by typing the
following commands:

    cd examples/multi-threading
    make
    ./multi-threading.native

rime/
-----

Contains a set of examples on how to use the Rime communications
stack. To run those examples in the 'netsim' target (a very simple
Contiki network simulator), compile the programs with:

    make TARGET=netsim

You will need to have GTK 1.x development libraries installed.

Run the different programs:

    ./test-abc.netsim
    ./test-meshroute.netsim
    ./test-rudolph0.netsim
    ./test-rudolph1.netsim
    ./test-treeroute.netsim
    ./test-trickle.netsim

Most of the examples requires you to click with the middle mouse
button on one of the simulated nodes for something to happen.

sky/
----

Examples inteded for running on the Tmote Sky board. To compile those, you need
to have msp430-gcc (the gcc C compiler for the MSP430 microcontroller)
installed.

The follwing programs are included:

- blink.c          A simple program that blinks the on-board LEDs
- sky-collect.c    Collects sensor data and energy profile values to a sink.
  Press the "user" button on the Tmote Sky that is connected to the PC to make
  the node a sink.
- test-button.c    Toggles the LEDs when the button is pressed.
- test-cfs.c       Tests the 1 mb flash memory of the Tmote Sky

telnet-server/
--------------

A simple TCP telnet server with a simple command shell. It can be
compiled and run in the 'minimal-net' target by typing the following
commands:

    cd examples/telnet-server
    make
    ./telnet-server.minimal-net

Most likely you'll have to adjust the TCP/IP values set in main() in
platform/minimal-net/contiki-main.c to match your needs.

Please consult cpu/native/net/README-WPCAP.md as well if you are running
Microsoft Windows.

waco-srdcp/
-----------

COOJA scenarios that integrate WaCo + SRDCP for data collection experiments on Sky motes. Build the firmware once:

    cd examples/waco-srdcp
    make example-runicast-srdcp.sky TARGET=sky

Then launch one of the provided simulations under `examples/waco-srdcp/sim/`:

* `waco-srdcp-grid-5-nodes/`, `waco-srdcp-grid-15-nodes/`, `waco-srdcp-grid-30-nodes/` – deterministic grid layouts.
* `waco-srdcp-random-5-nodes/`, `waco-srdcp-random-15-nodes/`, `waco-srdcp-random-30-nodes/` – sample random layouts that keep all nodes within the 50 m radio range.

For batch or headless runs, the Monte-Carlo helper scripts in the same folder wrap COOJA (`ant -nogui`) and parse the resulting logs. For example, to evaluate the random layouts with 20 seeds:

    cd examples/waco-srdcp/sim
    ./mc_random_5nodes.sh 20
    ./mc_random_15nodes.sh 20
    ./mc_random_30nodes.sh 20

Each script stores results under `examples/waco-srdcp/sim/out/waco-srdcp-random-*-nodes-mc` by default. They collect CSV summaries (PDR, neighbours, energy) for each seed and aggregate them across all runs.

webbrowser/
-----------

A text mode web browser supporting links and forms. It can be compiled
and run in the 'win32' target by typing the following commands:

    cd examples/webbrowser
    make
    ./webbrowser.win32

Most likely you'll have to adjust the TCP/IP values set in main() in
platform/win32/contiki-main.c to match your needs.

Please consult cpu/native/net/README-WPCAP.md as well.

webserver/
----------

A web server supporting dynamic content creation using "scripts" which
are actually compiled-in C-functions. It can be compiled and run in the
'minimal-net' target by typing the following commands:

    cd examples/webserver
    make
    ./webserver-example.minimal-net

As an alternative to the static and dynamic compiled-in content the web
server can instead support "external" static-only content loaded from
any storage supported by the 'Contiki File System' (CFS) interface. To
compile it in the 'minimal-net' target and have it load files from disk
use the following command:

    make HTTPD-CFS=1

Most likely you'll have to adjust the TCP/IP values set in main() in
platform/minimal-net/contiki-main.c to match your needs.

Please consult cpu/native/net/README-WPCAP.md as well if you are running
Microsoft Windows.

wget/
-----

A command line program that retrieves files from web servers and saves them
using the 'Contiki File System' (CFS). It can be compiled and run in the
'minimal-net' target by typing the following commands:

    cd examples/wget
    make
    ./wget.minimal-net

Most likely you'll have to adjust the TCP/IP values set in main() in
platform/minimal-net/contiki-main.c to match your needs.

Please consult cpu/native/net/README-WPCAP.md as well if you are running
Microsoft Windows.
