# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.27

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:

#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:

# Disable VCS-based implicit rules.
% : %,v

# Disable VCS-based implicit rules.
% : RCS/%

# Disable VCS-based implicit rules.
% : RCS/%,v

# Disable VCS-based implicit rules.
% : SCCS/s.%

# Disable VCS-based implicit rules.
% : s.%

.SUFFIXES: .hpux_make_needs_suffix_list

# Command-line flag to silence nested $(MAKE).
$(VERBOSE)MAKESILENT = -s

#Suppress display of executed commands.
$(VERBOSE).SILENT:

# A target that is always out of date.
cmake_force:
.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /home/lf/LF/cpp/KVStorageProjBaseRaft-cpp

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/cmake-build-debug

# Include any dependencies generated for this target.
include example/rpcExample/callee/CMakeFiles/provider.dir/depend.make
# Include any dependencies generated by the compiler for this target.
include example/rpcExample/callee/CMakeFiles/provider.dir/compiler_depend.make

# Include the progress variables for this target.
include example/rpcExample/callee/CMakeFiles/provider.dir/progress.make

# Include the compile flags for this target's objects.
include example/rpcExample/callee/CMakeFiles/provider.dir/flags.make

example/rpcExample/callee/CMakeFiles/provider.dir/friendService.cpp.o: example/rpcExample/callee/CMakeFiles/provider.dir/flags.make
example/rpcExample/callee/CMakeFiles/provider.dir/friendService.cpp.o: /home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/example/rpcExample/callee/friendService.cpp
example/rpcExample/callee/CMakeFiles/provider.dir/friendService.cpp.o: example/rpcExample/callee/CMakeFiles/provider.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/cmake-build-debug/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object example/rpcExample/callee/CMakeFiles/provider.dir/friendService.cpp.o"
	cd /home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/cmake-build-debug/example/rpcExample/callee && /usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT example/rpcExample/callee/CMakeFiles/provider.dir/friendService.cpp.o -MF CMakeFiles/provider.dir/friendService.cpp.o.d -o CMakeFiles/provider.dir/friendService.cpp.o -c /home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/example/rpcExample/callee/friendService.cpp

example/rpcExample/callee/CMakeFiles/provider.dir/friendService.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing CXX source to CMakeFiles/provider.dir/friendService.cpp.i"
	cd /home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/cmake-build-debug/example/rpcExample/callee && /usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/example/rpcExample/callee/friendService.cpp > CMakeFiles/provider.dir/friendService.cpp.i

example/rpcExample/callee/CMakeFiles/provider.dir/friendService.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling CXX source to assembly CMakeFiles/provider.dir/friendService.cpp.s"
	cd /home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/cmake-build-debug/example/rpcExample/callee && /usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/example/rpcExample/callee/friendService.cpp -o CMakeFiles/provider.dir/friendService.cpp.s

example/rpcExample/callee/CMakeFiles/provider.dir/__/friend.pb.cc.o: example/rpcExample/callee/CMakeFiles/provider.dir/flags.make
example/rpcExample/callee/CMakeFiles/provider.dir/__/friend.pb.cc.o: /home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/example/rpcExample/friend.pb.cc
example/rpcExample/callee/CMakeFiles/provider.dir/__/friend.pb.cc.o: example/rpcExample/callee/CMakeFiles/provider.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/cmake-build-debug/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Building CXX object example/rpcExample/callee/CMakeFiles/provider.dir/__/friend.pb.cc.o"
	cd /home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/cmake-build-debug/example/rpcExample/callee && /usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT example/rpcExample/callee/CMakeFiles/provider.dir/__/friend.pb.cc.o -MF CMakeFiles/provider.dir/__/friend.pb.cc.o.d -o CMakeFiles/provider.dir/__/friend.pb.cc.o -c /home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/example/rpcExample/friend.pb.cc

example/rpcExample/callee/CMakeFiles/provider.dir/__/friend.pb.cc.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing CXX source to CMakeFiles/provider.dir/__/friend.pb.cc.i"
	cd /home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/cmake-build-debug/example/rpcExample/callee && /usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/example/rpcExample/friend.pb.cc > CMakeFiles/provider.dir/__/friend.pb.cc.i

example/rpcExample/callee/CMakeFiles/provider.dir/__/friend.pb.cc.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling CXX source to assembly CMakeFiles/provider.dir/__/friend.pb.cc.s"
	cd /home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/cmake-build-debug/example/rpcExample/callee && /usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/example/rpcExample/friend.pb.cc -o CMakeFiles/provider.dir/__/friend.pb.cc.s

# Object files for target provider
provider_OBJECTS = \
"CMakeFiles/provider.dir/friendService.cpp.o" \
"CMakeFiles/provider.dir/__/friend.pb.cc.o"

# External object files for target provider
provider_EXTERNAL_OBJECTS =

/home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/bin/provider: example/rpcExample/callee/CMakeFiles/provider.dir/friendService.cpp.o
/home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/bin/provider: example/rpcExample/callee/CMakeFiles/provider.dir/__/friend.pb.cc.o
/home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/bin/provider: example/rpcExample/callee/CMakeFiles/provider.dir/build.make
/home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/bin/provider: /home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/lib/librpc_lib.a
/home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/bin/provider: example/rpcExample/callee/CMakeFiles/provider.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --bold --progress-dir=/home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/cmake-build-debug/CMakeFiles --progress-num=$(CMAKE_PROGRESS_3) "Linking CXX executable /home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/bin/provider"
	cd /home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/cmake-build-debug/example/rpcExample/callee && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/provider.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
example/rpcExample/callee/CMakeFiles/provider.dir/build: /home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/bin/provider
.PHONY : example/rpcExample/callee/CMakeFiles/provider.dir/build

example/rpcExample/callee/CMakeFiles/provider.dir/clean:
	cd /home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/cmake-build-debug/example/rpcExample/callee && $(CMAKE_COMMAND) -P CMakeFiles/provider.dir/cmake_clean.cmake
.PHONY : example/rpcExample/callee/CMakeFiles/provider.dir/clean

example/rpcExample/callee/CMakeFiles/provider.dir/depend:
	cd /home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/cmake-build-debug && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/lf/LF/cpp/KVStorageProjBaseRaft-cpp /home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/example/rpcExample/callee /home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/cmake-build-debug /home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/cmake-build-debug/example/rpcExample/callee /home/lf/LF/cpp/KVStorageProjBaseRaft-cpp/cmake-build-debug/example/rpcExample/callee/CMakeFiles/provider.dir/DependInfo.cmake "--color=$(COLOR)"
.PHONY : example/rpcExample/callee/CMakeFiles/provider.dir/depend

