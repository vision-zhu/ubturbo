# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/root/ubturbo/3rdparty/spdlog"
  "/root/ubturbo/3rdparty/spdlog-prefix/src/spdlog-build"
  "/root/ubturbo/3rdparty/spdlog-prefix"
  "/root/ubturbo/3rdparty/spdlog-prefix/tmp"
  "/root/ubturbo/3rdparty/spdlog-prefix/src/spdlog-stamp"
  "/root/ubturbo/3rdparty/spdlog-prefix/src"
  "/root/ubturbo/3rdparty/spdlog-prefix/src/spdlog-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/root/ubturbo/3rdparty/spdlog-prefix/src/spdlog-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/root/ubturbo/3rdparty/spdlog-prefix/src/spdlog-stamp${cfgdir}") # cfgdir has leading slash
endif()
