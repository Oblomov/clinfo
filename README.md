# What is this?

clinfo is a simple command-line application that enumerates all possible
(known) properties of the OpenCL platform and devices available on the
system.

Inspired by AMD's program of the same name, it is coded in pure C and it
tries to output all possible information, including those provided by
platform-specific extensions, trying not to crash on unsupported
properties (e.g. 1.2 properties on 1.1 platforms).

# Usage

    clinfo [options...]

Common used options are `-l` to show a synthetic summary of the
available devices (without properties), and `-a`, to try and show
properties even if `clinfo` would otherwise think they aren't supported
by the platform or device.

Refer to the man page for further information.

## Use cases

* verify that your OpenCL environment is set up correctly;
  if `clinfo` cannot find any platform or devices (or fails to load
  the OpenCL dispatcher library), chances are high no other OpenCL
  application will run;
* verify that your OpenCL _development_ environment is set up
  correctly: if `clinfo` fails to build, chances are high no
  other OpenCL application will build;
* explore/report the actual properties of the available device(s).

## Segmentation faults

Some faulty OpenCL platforms may cause `clinfo` to crash. There isn't
much `clinfo` itself can do about it, but you can try and isolate the
platform responsible for this. On POSIX systems, you can generally find
the platform responsible for the fault with the following one-liner:

    find /etc/OpenCL/vendors/ -name '*.icd' | while read OPENCL_VENDOR_PATH ; do clinfo -l > /dev/null ; echo "$? ${OPENCL_VENDOR_PATH}" ; done

# Building

<img
src='https://api.travis-ci.org/Oblomov/clinfo.svg?branch=master'
alt='Build status on Travis'
style='float: right'>

Building requires an OpenCL SDK (or at least OpenCL headers and
development files), and the standard build environment for the platform.
No special build system is used (autotools, CMake, meson, ninja, etc),
as I feel adding more dependencies for such a simple program would be
excessive. Simply running `make` at the project root should work.

## Windows support

The application can usually be built in Windows too (support for which
required way more time than I should have spent, really, but I digress),
by running `make` in a Developer Command Prompt for Visual Studio,
provided an OpenCL SDK (such as the Intel or AMD one) is installed.

Precompiled Windows executable are available as artefacts of the
AppVeyor CI.

<table style='margin: 1em auto; width: 100%; max-width: 33em'>
<tr><th>Build status</th><th colspan=2>Windows binaries</th></tr>
<tr>
<td><a href='https://ci.appveyor.com/project/Oblomov/clinfo/'><img
src='https://ci.appveyor.com/api/projects/status/github/Oblomov/clinfo?svg=true'
alt='Build status on AppVeyor'></a></td>
<td><a href='https://ci.appveyor.com/api/projects/oblomov/clinfo/artifacts/clinfo.exe?job=platform%3a+x86'>32-bit</a></td>
<td><a href='https://ci.appveyor.com/api/projects/oblomov/clinfo/artifacts/clinfo.exe?job=platform%3a+x64'>64-bit</a></td>
</tr>
</table>
