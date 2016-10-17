This is a basic utility (designed to run as a service in windows) that will 
synchronize a remote network share with a local directory.

The impetus for creating this tool is that older CNC machines only have NETBEUI 
network functionality for their network shares, which results in a limitation
of using Windows Vista x86 or Windows Server 2008 x86 for the newest possible
operating system.

This is designed to be run on a server, or virtual machine running either Vista
x86 or 2008 x86 (Not 2008R2, which is can no longer load a NETBEUI driver),
unless the network shares being mapped are TCPIP, which I have not tested or
use at all, but should work without issue.

There is no configuration required, and no configuration file.
For usage create the directory C:\sync, and then under that directory add
subdirectories in the format machine\share, and they will be mapped
automatically. If you are using NETBEUI, make sure to install the drivers for
that, which are last found on the Windows XP installation cd, but will work in
Vista x86 and Server 2008 x86 as well.

After that it will map each share as a network drive, starting with G: (which 
also ends up meaning that there is a limit of 20 shares, assuming you don't
have any drives past F:). Each share runs in a different thread, and 
synchronizes in both direction, with preference for the files in the local 
directory in the event of a collision (I think, don't remember.)

In use there have been occasional issues with failing on CopyFile with never
ending error 5s, but this does not happen often, and will be corrected to not
cause issues in a future push.

As this is was written with CNC machines in mind, this would allow you to run
the CAD/CAM for the CNC on any operating system, and a use case would be when 
sending files after the postprocessor, instead of sending it directly to the 
CNC (i.e. \\nc0001\diskf), you would create a share for C:\sync on the new 
virtual (or physical) server, and point to that instead 
(i.e. \\server\sync\nc0001\diskf)

In the event there is an error with copying or mapping the network drive, it
will attempt to disconnect and reconnect the network drive, and during the
time it is offline, it will write a file +OFFLINE.TXT with the error number
in the file, so as to indicate the share is offline.

I've included a macro RUNASSERVICE in main.cpp which if defined will compile
to run as a service, if not defined will not (currently in the initial import 
it is not defined to run as a service).

As this is to run on Windows, it was written and compiles using MSVC++ 14.0
(Visual Studio 2015)
