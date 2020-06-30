#
# project: skynet-study
#
cmake_minimum_required(VERSION 2.8)
project(skynet-study)
# include the fips main cmake file
get_filename_component(FIPS_ROOT_DIR "../fips" ABSOLUTE)
include("${FIPS_ROOT_DIR}/cmake/fips.cmake")

# include_directories(src)
fips_setup()

fips_include_directories(3rd/lua)
fips_include_directories(3rd/jemalloc/include)

fips_ide_group(Ext)
fips_add_subdirectory(3rd)

fips_ide_group(Module)
fips_add_subdirectory(service-src)

fips_ide_group(LuaAPI)
fips_add_subdirectory(lualib-src)

fips_ide_group(Skynet)
fips_add_subdirectory(skynet-src)

# # lua library
# fips_ide_group(lua-lib)
# fips_add_subdirectory(lualib)

# # lua service
# fips_ide_group(lua-service)
# fips_add_subdirectory(service)

# # example
# fips_ide_group(lua-examples)
# fips_add_subdirectory(examples)

# # test
# fips_ide_group(lua-test)
# fips_add_subdirectory(test)

fips_finish()


