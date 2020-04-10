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

## Missing information

If you know of device properties that are exposed in OpenCL (either as core
properties or as extensions), but are not shown by `clinfo`, please [open
an issue](https://github.com/Oblomov/clinfo/issues) providing as much
information as you can. Patches and pull requests accepted too.


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

## Android support

### Local build via Termux

One way to build the application on Android, pioneered by
[truboxl][truboxl] and described [here][issue46], requires the
installation of [Termux][termux], that can be installed via Google Play
as well as via F-Droid.

[truboxl]: https://github.com/truboxl
[issue46]: https://github.com/Oblomov/clinfo/issues/46
[termux]: https://termux.com/

Inside Termux, you will first need to install some common tools:

	pkg install git make clang -y


You will also need to clone the `clinfo` repository, and fetch the
OpenCL headers (we'll use the official `KhronosGroup/OpenCL-Headers`
repository for that):

	git clone https://github.com/Oblomov/clinfo
	git clone https://github.com/KhronosGroup/OpenCL-Headers

(I prefer doing this from a `src` directory I have created for
development, but as long as `clinfo` and `OpenCL-Headers` are sibling
directories, the headers will be found. If not, you will have to
override `CPPFLAGS` with e.g. `export CPPFLAGS=/path/to/where/headers/are`
before running `make`.)

You can then `cd clinfo` and build the application with

	make OS=Android

(The `OS` value must be specified because currently Android is not autodetected.)

If linking fails due to a missing `libOpenCL.so`, then your Android
machine probably doesn't support OpenCL. Otherwise, you should have a
working `clinfo` you can run. You will most probably need to set
`LD_LIBRARY_PATH` to let the program know where the OpenCL library is at
runtime: you will need at least `${ANDROID_ROOT}/vendor/lib64`, but on
some machine the OpenCL library actually maps to a different library
(e.g., on one of my systems, it maps to the GLES library, which is in a
different subdirectory). Something like:

	LD_LIBRARY_PATH="${LD_LIBRARY_PATH}:${ANDROID_ROOT}/vendor/lib64:${ANDROID_ROOT}/vendor/lib64/egl" ./clinfo

might work.

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
