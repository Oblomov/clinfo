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

# Building

Building requires an OpenCL SDK (or at least OpenCL headers and
development files), and the standard build environment for the
platform. No special build system is used (autotools, CMake, meson,
ninja, etc), as I feel it would be overengineering for such a simple
program. Simply running `make` at the project root should work.

## Windows support

The application can usually be built in Windows too (support for which
required way more time than I should have spent, really, but I digress),
by running `make` in a Developer Command Prompt for Visual Studio,
provided an OpenCL SDK (such as the Intel or AMD one) is installed.

Precompiled Windows executable are available as artefacts of the
AppVeyor CI.

<table style='margin: 1em auto; width: 100%; max-width: 33em'>
<style>th,td{text-align: center}</style>
<tr><th>Build status</th><th colspan=2>Windows binaries</th></tr>
<tr>
<td><a href='https://ci.appveyor.com/project/Oblomov/clinfo/'><img
src='https://ci.appveyor.com/api/projects/status/github/Oblomov/clinfo?svg=true'
alt='Build status on AppVeyor'></a></td>
<td><a href='https://ci.appveyor.com/api/projects/oblomov/clinfo/artifacts/clinfo.exe?job=platform%3a+x86'>32-bit</a></td>
<td><a href='https://ci.appveyor.com/api/projects/oblomov/clinfo/artifacts/clinfo.exe?job=platform%3a+x64'>64-bit</a></td>
</tr>
</table>
