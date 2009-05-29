****** Threadnetperf ****** 
A multi-threaded network benchmark tool
 by  Andrew Brampton <brampton@gmail.com> (2007-2009)
 and Mathew Faulkner (2007-2009)
	
Threadnetperf is a highly customisable high performance network benchmarking tool. The key difference from previous tools is that the user is able to control how many threads threadnetperf uses, as well as which cores these threads are pinned to. This tool was useful in our research for measuring the effect of sending or receiving from one core, and having the OS network stack run on another core. Additionally the tool can be configured to use an unlimited number of connections, threads or processes, which scale considerably well due to the use of the epoll API.
 
Threadnetperf has previously worked on Windows, Linux, and FreeBSD, however, we do not test it very rigorously so one of these platforms may be broken. All patches are welcome.
 
To build on a *nix style system just use the simple "Makefile" by executing the command "make" or "gmake". To build on Windows just use the provided threadnetperf.2008.sln file.
 
All the commands of Threadnetperf are documented within the application, but a quick overview is here:

Usage: threadnetperf [options] tests
Usage: threadnetperf -D [options]
       -c level,interval   Confidence level, must be 95 or 99
       -D         Use daemon mode (wait for incoming tests)
       -d time    Set duration to run the test for
       -e         Eat the data (i.e. dirty it)
       -H host    Set the remote host(and port) to connect to
       -h         Display this help
       -i min,max Set the minimum and maximum iterations
       -m [t,p]   What programming model to use, [thread or process]
       -n         Disable Nagle's algorithm (e.g no delay)
       -p port    Set the port number for the first server thread to use
       -s size    Set the send/recv size
       -T         Timestamp packets, and measure latency (only available on *nix)
       -t         Use TCP
       -r         Packets per second rate (default: ~0)
       -u         Use UDP
       -v         Verbose
       -V         Display version only

       tests      Combination of cores and clients
       tests      Core numbers are masks, for example 1 is core 0, 3 is core 0 and core 1
               N{c-s}   N connections
                        c client cores mask
                        s server cores mask

You can run tests locally, or across two machines. Here are some examples:
 
    threadnetperf -n -s 10000 1{1-1}
 
Will run a local TCP test, with Nagle's algorithm disabled, a send size of 10,000, and 1 TCP connection between cores 0 and 0.
 
    threadnetperf 10{1-1} 10{2-2} 10{4-4}
 
Will again run a local TCP test, however this time 10 connection from core 0 to core 0, 10 connections from core 1 to core 1, and 10 connections from core 2 to core 2

To run across a network just start the threadnetperf daemon on one of the machines like so:
 
     threadnetperf -D
 
and on the other machine execute:

     threadnetperf -H serverIP 1{1-2}

which will conduct a TCP test from the client to the server, with one thread executing on client's core 0, and the server's core 1.

There is no need to give the threadnetperf daemon any options, as the options configured on the server will be passed over the network. This make running many tests very easy as the daemon will continue to wait for new tests until it is told otherwise.

If you find this tool useful, or you have any suggestions for improvements then please contact us.