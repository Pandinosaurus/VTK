if (FALSE) # XXX(kitware): use VTK's module system
cmake_minimum_required(VERSION 3.18)
endif ()

file(STRINGS "configure.ac" CONFIGURE_AC_LINES)
foreach(line ${CONFIGURE_AC_LINES})
    if(line MATCHES [[^m4_define\(\[(MAJOR_VERSION|MINOR_VERSION|MICRO_VERSION)\],[ \t]*([0-9]+)\)$]])
        set(LIBXML_${CMAKE_MATCH_1} ${CMAKE_MATCH_2})
    elseif(line MATCHES "^(LIBXML_MAJOR_VERSION|LIBXML_MINOR_VERSION|LIBXML_MICRO_VERSION)=([0-9]+)$")
        set(${CMAKE_MATCH_1} ${CMAKE_MATCH_2})
    endif()
endforeach()
set(VERSION "${LIBXML_MAJOR_VERSION}.${LIBXML_MINOR_VERSION}.${LIBXML_MICRO_VERSION}")

project(libxml2 VERSION ${VERSION} LANGUAGES C)

include(CheckCSourceCompiles)
include(CheckFunctionExists)
include(CheckIncludeFiles)
include(CheckLibraryExists)
if (FALSE) # XXX(kitware): Requires bumping minimum CMake version to 3.18
include(CheckLinkerFlag)
endif()
include(CheckStructHasMember)
include(CheckSymbolExists)
include(CMakePackageConfigHelpers)
include(FindPkgConfig)
include(GNUInstallDirs)

if (FALSE) # XXX(kitware): Hardcode settings.
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
set(LIBXML2_WITH_AUTOMATA ON)
option(LIBXML2_WITH_C14N "Add the Canonicalization support" ON)
option(LIBXML2_WITH_CATALOG "Add the Catalog support" ON)
option(LIBXML2_WITH_DEBUG "Add the debugging module" ON)
set(LIBXML2_WITH_EXPR ON)
option(LIBXML2_WITH_FTP "Add the FTP support" OFF)
option(LIBXML2_WITH_HTML "Add the HTML support" ON)
option(LIBXML2_WITH_HTTP "Add the HTTP support" OFF)
option(LIBXML2_WITH_ICONV "Add ICONV support" ON)
option(LIBXML2_WITH_ICU "Add ICU support" OFF)
option(LIBXML2_WITH_ISO8859X "Add ISO8859X support if no iconv" ON)
option(LIBXML2_WITH_LEGACY "Add deprecated APIs for compatibility" OFF)
option(LIBXML2_WITH_LZMA "Use liblzma" OFF)
option(LIBXML2_WITH_MODULES "Add the dynamic modules support" ON)
option(LIBXML2_WITH_OUTPUT "Add the serialization support" ON)
option(LIBXML2_WITH_PATTERN "Add the xmlPattern selection interface" ON)
option(LIBXML2_WITH_PROGRAMS "Build programs" ON)
option(LIBXML2_WITH_PUSH "Add the PUSH parser interfaces" ON)
option(LIBXML2_WITH_PYTHON "Build Python bindings" ON)
option(LIBXML2_WITH_READER "Add the xmlReader parsing interface" ON)
option(LIBXML2_WITH_REGEXPS "Add Regular Expressions support" ON)
option(LIBXML2_WITH_SAX1 "Add the older SAX1 interface" ON)
option(LIBXML2_WITH_SCHEMAS "Add Relax-NG and Schemas support" ON)
option(LIBXML2_WITH_SCHEMATRON "Add Schematron support" ON)
option(LIBXML2_WITH_TESTS "Build tests" ON)
option(LIBXML2_WITH_THREADS "Add multithread support" ON)
option(LIBXML2_WITH_THREAD_ALLOC "Add per-thread memory" OFF)
option(LIBXML2_WITH_TLS "Enable thread-local storage" OFF)
option(LIBXML2_WITH_TREE "Add the DOM like tree manipulation APIs" ON)
set(LIBXML2_WITH_UNICODE ON)
option(LIBXML2_WITH_VALID "Add the DTD validation support" ON)
option(LIBXML2_WITH_WRITER "Add the xmlWriter saving interface" ON)
option(LIBXML2_WITH_XINCLUDE "Add the XInclude support" ON)
option(LIBXML2_WITH_XPATH "Add the XPATH support" ON)
option(LIBXML2_WITH_XPTR "Add the XPointer support" ON)
option(LIBXML2_WITH_XPTR_LOCS "Add support for XPointer locations" OFF)
option(LIBXML2_WITH_ZLIB "Use libz" OFF)
set(LIBXML2_XMLCONF_WORKING_DIR ${CMAKE_CURRENT_BINARY_DIR} CACHE PATH "Working directory for XML Conformance Test Suite")
else ()
set(LIBXML2_WITH_AUTOMATA ON)
set(LIBXML2_WITH_C14N ON)
set(LIBXML2_WITH_CATALOG ON)
set(LIBXML2_WITH_DEBUG OFF)
set(LIBXML2_WITH_DOCB ON)
set(LIBXML2_WITH_EXPR ON)
set(LIBXML2_WITH_FTP OFF)
set(LIBXML2_WITH_HTML ON)
set(LIBXML2_WITH_HTTP OFF)
set(LIBXML2_WITH_ICONV OFF)
set(LIBXML2_WITH_ICU OFF)
set(LIBXML2_WITH_ISO8859X ON)
set(LIBXML2_WITH_LEGACY OFF)
set(LIBXML2_WITH_LZMA OFF)
set(LIBXML2_WITH_MEM_DEBUG OFF)
set(LIBXML2_WITH_MODULES ON)
set(LIBXML2_WITH_OUTPUT ON)
set(LIBXML2_WITH_PATTERN ON)
set(LIBXML2_WITH_PROGRAMS OFF)
set(LIBXML2_WITH_PUSH ON)
set(LIBXML2_WITH_PYTHON OFF)
set(LIBXML2_WITH_READER ON)
set(LIBXML2_WITH_REGEXPS ON)
set(LIBXML2_WITH_RUN_DEBUG OFF)
set(LIBXML2_WITH_SAX1 ON)
set(LIBXML2_WITH_SCHEMAS ON)
set(LIBXML2_WITH_SCHEMATRON ON)
set(LIBXML2_WITH_TESTS OFF)
set(LIBXML2_WITH_THREADS ON)
set(LIBXML2_WITH_THREAD_ALLOC ON)
set(LIBXML2_WITH_TREE ON)
set(LIBXML2_WITH_TRIO OFF)
set(LIBXML2_WITH_UNICODE ON)
set(LIBXML2_WITH_VALID ON)
set(LIBXML2_WITH_WRITER ON)
set(LIBXML2_WITH_XINCLUDE ON)
set(LIBXML2_WITH_XPATH ON)
set(LIBXML2_WITH_XPTR ON)
set(LIBXML2_WITH_ZLIB ON)
set(LIBXML2_XMLCONF_WORKING_DIR ${CMAKE_CURRENT_BINARY_DIR})
endif ()

if(LIBXML2_WITH_PYTHON)
    check_include_files(unistd.h HAVE_UNISTD_H)
    check_symbol_exists(F_GETFL fcntl.h HAVE_F_GETFL)
    find_package(Python COMPONENTS Interpreter Development REQUIRED)
    #set(LIBXML2_PYTHON_INSTALL_DIR ${Python_SITEARCH} CACHE PATH "Python bindings install directory")
    set(LIBXML2_PYTHON_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/python"
        CACHE PATH "Python bindings install directory")
endif()

foreach(VARIABLE IN ITEMS WITH_AUTOMATA WITH_C14N WITH_CATALOG WITH_DEBUG WITH_EXPR WITH_FTP WITH_HTML WITH_HTTP WITH_ICONV WITH_ICU WITH_ISO8859X WITH_LEGACY WITH_LZMA WITH_MODULES WITH_OUTPUT WITH_PATTERN WITH_PUSH WITH_READER WITH_REGEXPS WITH_SAX1 WITH_SCHEMAS WITH_SCHEMATRON WITH_THREADS WITH_THREAD_ALLOC WITH_TREE WITH_UNICODE WITH_VALID WITH_WRITER WITH_XINCLUDE WITH_XPATH WITH_XPTR WITH_XPTR_LOCS WITH_ZLIB)
    if(LIBXML2_${VARIABLE})
        set(${VARIABLE} 1)
    else()
        set(${VARIABLE} 0)
    endif()
endforeach()

set(LIBXML_VERSION ${VERSION})
set(LIBXML_VERSION_EXTRA "")
math(EXPR LIBXML_VERSION_NUMBER "
    ${LIBXML_MAJOR_VERSION} * 10000 +
    ${LIBXML_MINOR_VERSION} * 100 +
    ${LIBXML_MICRO_VERSION}
")

set(MODULE_EXTENSION "${CMAKE_SHARED_LIBRARY_SUFFIX}")

set(PACKAGE "libxml2")
set(PACKAGE_NAME "libxml2")
set(PACKAGE_STRING "libxml2 ${VERSION}")
set(PACKAGE_TARNAME "libxml2")
set(PACKAGE_URL "https://gitlab.gnome.org/GNOME/libxml2")
set(PACKAGE_VERSION ${VERSION})

if(LIBXML2_WITH_ICONV)
    find_package(Iconv REQUIRED)
endif()

if(LIBXML2_WITH_ICU)
    find_package(ICU REQUIRED COMPONENTS data i18n uc)
endif()

if(LIBXML2_WITH_LZMA)
    find_package(LibLZMA REQUIRED)
endif()

if(LIBXML2_WITH_THREADS)
    find_package(Threads REQUIRED)
    set(THREAD_LIBS ${CMAKE_THREAD_LIBS_INIT})
    list(APPEND CMAKE_REQUIRED_LIBRARIES Threads::Threads)
endif()

if(LIBXML2_WITH_ZLIB)
  # find_package(ZLIB REQUIRED)
endif()

if (NOT MSVC)
    check_c_source_compiles("
        void __attribute__((destructor))
        f(void) {}
        int main(void) { return 0; }
    " HAVE_ATTRIBUTE_DESTRUCTOR)
    if(HAVE_ATTRIBUTE_DESTRUCTOR)
        set(ATTRIBUTE_DESTRUCTOR "__attribute__((destructor))")
    endif()
    check_include_files(arpa/inet.h HAVE_ARPA_INET_H)
    check_function_exists(class HAVE_CLASS)
    check_include_files(dlfcn.h HAVE_DLFCN_H)
    check_library_exists(dl dlopen "" HAVE_DLOPEN)
    check_include_files(dl.h HAVE_DL_H)
    check_include_files(fcntl.h HAVE_FCNTL_H)
    check_function_exists(fpclass HAVE_FPCLASS)
    check_function_exists(ftime HAVE_FTIME)
    check_function_exists(getentropy HAVE_GETENTROPY)
    check_function_exists(gettimeofday HAVE_GETTIMEOFDAY)
    check_library_exists(history append_history "" HAVE_LIBHISTORY)
    check_library_exists(readline readline "" HAVE_LIBREADLINE)
    check_function_exists(mmap HAVE_MMAP)
    check_function_exists(munmap HAVE_MUNMAP)
    check_include_files(netdb.h HAVE_NETDB_H)
    check_include_files(netinet/in.h HAVE_NETINET_IN_H)
    check_include_files(poll.h HAVE_POLL_H)
    check_library_exists(dld shl_load "" HAVE_SHLLOAD)
    check_function_exists(stat HAVE_STAT)
    check_include_files(stdint.h HAVE_STDINT_H)
    check_include_files(sys/mman.h HAVE_SYS_MMAN_H)
    if (APPLE)
        # In old macOS SDKs (ex: 10.15), sys/random.h fails to include header files it needs, so add them here.
        check_include_files("Availability.h;stddef.h;sys/random.h" HAVE_SYS_RANDOM_H)
    else()
        check_include_files(sys/random.h HAVE_SYS_RANDOM_H)
    endif()
    check_include_files(sys/select.h HAVE_SYS_SELECT_H)
    check_include_files(sys/socket.h HAVE_SYS_SOCKET_H)
    check_include_files(sys/stat.h HAVE_SYS_STAT_H)
    check_include_files(sys/timeb.h HAVE_SYS_TIMEB_H)
    check_include_files(sys/time.h HAVE_SYS_TIME_H)
    check_include_files(unistd.h HAVE_UNISTD_H)
    check_c_source_compiles("
        #include <stddef.h>
        #include <sys/socket.h>
        int main() { (void) getsockopt(1, 1, 1, NULL, (socklen_t*) NULL); return 0; }
    " XML_SOCKLEN_T_SOCKLEN_T)
    if(XML_SOCKLEN_T_SOCKLEN_T)
        set(XML_SOCKLEN_T socklen_t)
    else()
        check_c_source_compiles("
            #include <stddef.h>
            #include <sys/socket.h>
            int main() { (void) getsockopt(1, 1, 1, NULL, (size_t*) NULL); return 0; }
        " XML_SOCKLEN_T_SIZE_T)
        if(XML_SOCKLEN_T_SIZE_T)
            set(XML_SOCKLEN_T size_t)
        else()
            check_c_source_compiles("
                #include <stddef.h>
                #include <sys/socket.h>
                int main() { (void) getsockopt (1, 1, 1, NULL, (int*) NULL); return 0; }
            " XML_SOCKLEN_T_INT)
            set(XML_SOCKLEN_T int)
        endif()
    endif()
endif()

if(LIBXML2_WITH_TLS)
    check_c_source_compiles(
        "_Thread_local int v; int main(){return 0;}"
        XML_THREAD_LOCAL_C11
    )
    if (XML_THREAD_LOCAL_C11)
        set(XML_THREAD_LOCAL "_Thread_local")
    else()
        check_c_source_compiles(
            "__thread int v; int main(){return 0;}"
            XML_THREAD_LOCAL_THREAD
        )
        if (XML_THREAD_LOCAL_THREAD)
            set(XML_THREAD_LOCAL "__thread")
        else()
            check_c_source_compiles(
                "__declspec(thread) int v; int main(){return 0;}"
                XML_THREAD_LOCAL_DECLSPEC
            )
            if (XML_THREAD_LOCAL_DECLSPEC)
                set(XML_THREAD_LOCAL "__declspec(thread)")
            endif()
        endif()
    endif()
endif()

set(
	LIBXML2_HDRS
	include/libxml/c14n.h
	include/libxml/catalog.h
	include/libxml/chvalid.h
	include/libxml/debugXML.h
	include/libxml/dict.h
	include/libxml/encoding.h
	include/libxml/entities.h
	include/libxml/globals.h
	include/libxml/hash.h
	include/libxml/HTMLparser.h
	include/libxml/HTMLtree.h
	include/libxml/list.h
	include/libxml/nanoftp.h
	include/libxml/nanohttp.h
	include/libxml/parser.h
	include/libxml/parserInternals.h
	include/libxml/pattern.h
	include/libxml/relaxng.h
	include/libxml/SAX.h
	include/libxml/SAX2.h
	include/libxml/schemasInternals.h
	include/libxml/schematron.h
	include/libxml/threads.h
	include/libxml/tree.h
	include/libxml/uri.h
	include/libxml/valid.h
	include/libxml/vtk_libxml2_mangle.h
	include/libxml/xinclude.h
	include/libxml/xlink.h
	include/libxml/xmlIO.h
	include/libxml/xmlautomata.h
	include/libxml/xmlerror.h
	include/libxml/xmlexports.h
	include/libxml/xmlmemory.h
	include/libxml/xmlmodule.h
	include/libxml/xmlreader.h
	include/libxml/xmlregexp.h
	include/libxml/xmlsave.h
	include/libxml/xmlschemas.h
	include/libxml/xmlschemastypes.h
	include/libxml/xmlstring.h
	include/libxml/xmlunicode.h
	include/libxml/xmlwriter.h
	include/libxml/xpath.h
	include/libxml/xpathInternals.h
	include/libxml/xpointer.h
	"${CMAKE_CURRENT_BINARY_DIR}/include/libxml/xmlversion.h"
)

set(
    LIBXML2_SRCS
    buf.c
    c14n.c
    catalog.c
    chvalid.c
    debugXML.c
    dict.c
    encoding.c
    entities.c
    error.c
    globals.c
    hash.c
    HTMLparser.c
    HTMLtree.c
    legacy.c
    list.c
    nanoftp.c
    nanohttp.c
    parser.c
    parserInternals.c
    pattern.c
    relaxng.c
    SAX.c
    SAX2.c
    schematron.c
    threads.c
    tree.c
    uri.c
    valid.c
    xinclude.c
    xlink.c
    xmlIO.c
    xmlmemory.c
    xmlmodule.c
    xmlreader.c
    xmlregexp.c
    xmlsave.c
    xmlschemas.c
    xmlschemastypes.c
    xmlstring.c
    xmlunicode.c
    xmlwriter.c
    xpath.c
    xpointer.c
    xzlib.c
)

if(WIN32)
    list(APPEND LIBXML2_SRCS win32/libxml2.rc)
    file(
        WRITE
        ${CMAKE_CURRENT_BINARY_DIR}/rcVersion.h
        "#define LIBXML_MAJOR_VERSION ${LIBXML_MAJOR_VERSION}\n"
        "#define LIBXML_MINOR_VERSION ${LIBXML_MINOR_VERSION}\n"
        "#define LIBXML_MICRO_VERSION ${LIBXML_MICRO_VERSION}\n"
        "#define LIBXML_DOTTED_VERSION \"${VERSION}\"\n"
    )
endif()

if (FALSE) # XXX(kitware): use VTK's module system
add_library(LibXml2 ${LIBXML2_HDRS} ${LIBXML2_SRCS})
add_library(LibXml2::LibXml2 ALIAS LibXml2)

target_compile_definitions(LibXml2 PRIVATE SYSCONFDIR="${CMAKE_INSTALL_FULL_SYSCONFDIR}")

if(LIBXML2_WITH_THREADS)
    if(NOT WIN32)
        check_include_files(pthread.h HAVE_PTHREAD_H)
    endif()
endif()

target_include_directories(
    LibXml2
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${CMAKE_INSTALL_INCLUDEDIR}/libxml2>
)
else ()
vtk_module_add_module(VTK::libxml2
  HEADER_DIRECTORIES
  SOURCES ${LIBXML2_SRCS}
  HEADERS ${LIBXML2_HDRS}
  HEADERS_SUBDIR "vtklibxml2")
target_compile_definitions(libxml2
  PRIVATE
    _GNU_SOURCE
    _REENTRANT)

if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(libxml2
    PRIVATE
      LIBXML_STATIC)
endif()

target_include_directories(libxml2
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include/libxml"
    "${CMAKE_CURRENT_BINARY_DIR}/include/libxml"
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>"
    "$<INSTALL_INTERFACE:${_vtk_build_HEADERS_DESTINATION}/vtklibxml2/include>")
endif ()

if(LIBXML2_WITH_MODULES)
    if(HAVE_DLOPEN)
        # target_link_libraries(LibXml2 PRIVATE dl)
        # set(MODULE_PLATFORM_LIBS "-ldl")
        vtk_module_link(VTK::libxml2 PRIVATE dl)
    endif()

    if(HAVE_SHLLOAD)
        # target_link_libraries(LibXml2 PRIVATE dld)
        # set(MODULE_PLATFORM_LIBS "-ldld")
        vtk_module_link(VTK::libxml2 PRIVATE dld)
    endif()
endif()

if(UNIX)
    # target_link_libraries(LibXml2 PRIVATE m)
    # set(LIBM "-lm")
    vtk_module_link(VTK::libxml2 PRIVATE m)
endif()

if(WIN32)
    # target_link_libraries(LibXml2 PRIVATE ws2_32)
    # set(WINSOCK_LIBS "-lws2_32")
    vtk_module_link(VTK::libxml2 PRIVATE ws2_32)
    # target_link_libraries(LibXml2 PRIVATE bcrypt)
    # set(CRYPTO_LIBS "-lbcrypt")
    vtk_module_link(VTK::libxml2 PRIVATE bcrypt)
endif()

if(LIBXML2_WITH_ICONV)
    # target_link_libraries(LibXml2 PUBLIC Iconv::Iconv)
    # if(NOT Iconv_IS_BUILT_IN)
    #     set(ICONV_LIBS "-liconv")
    #  endif()
    vtk_module_link(VTK::libxml2 PUBLIC Iconv::Iconv)
endif()

if(LIBXML2_WITH_ICU)
    # target_link_libraries(LibXml2 PRIVATE ICU::data ICU::i18n ICU::uc)
    # if(WIN32)
    #     set(ICU_LDFLAGS "-licudt -licuin -licuuc")
    # else()
    #     set(ICU_LDFLAGS "-licudata -licui18n -licuuc")
    # endif()
    vtk_module_link(VTK::libxml2 PRIVATE ICU::data ICU::i18n ICU::uc)
    # list(APPEND XML_PRIVATE_LIBS "${ICU_LDFLAGS}")
    # pkg_check_modules(ICU_PC IMPORTED_TARGET icu-i18n)
    # if(ICU_PC_FOUND)
    #     list(APPEND XML_PC_REQUIRES icu-i18n)
    # else()
    #     list(APPEND XML_PC_LIBS "${ICU_LDFLAGS}")
    # endif()
endif()

if(LIBXML2_WITH_LZMA)
    # target_link_libraries(LibXml2 PRIVATE LibLZMA::LibLZMA)
    # set(LibLZMA_LDFLAGS "-llzma")
    vtk_module_link(VTK::libxml2 PRIVATE LibLZMA::LibLZMA)
    # list(APPEND XML_PRIVATE_LIBS "${LibLZMA_LDFLAGS}")
    # pkg_check_modules(LibLZMA_PC IMPORTED_TARGET liblzma)
    # if(LibLZMA_PC_FOUND)
    #     list(APPEND XML_PC_REQUIRES liblzma)
    # else()
    #     list(APPEND XML_PC_LIBS "${LibLZMA_LDFLAGS}")
    # endif()
endif()

if(LIBXML2_WITH_THREADS)
	# target_link_libraries(LibXml2 PRIVATE Threads::Threads)
	# set(THREAD_LIBS ${CMAKE_THREAD_LIBS_INIT})
	vtk_module_link(VTK::libxml2 PRIVATE Threads::Threads)
endif()

if(LIBXML2_WITH_ZLIB)
	# target_link_libraries(LibXml2 PRIVATE ZLIB::ZLIB)
	# set(Z_LIBS "-lz")
	vtk_module_link(VTK::libxml2 PRIVATE VTK::zlib)
  #  list(APPEND XML_PRIVATE_LIBS "${ZLIB_LDFLAGS}")
  #  pkg_check_modules(ZLIB_PC IMPORTED_TARGET zlib)
  #  if(ZLIB_PC_FOUND)
  #    list(APPEND XML_PC_REQUIRES zlib)
  #  else()
  #    list(APPEND XML_PC_LIBS "${ZLIB_LDFLAGS}")
  #  endif()
endif()

if (FALSE) # XXX(kitware): handled by VTK's module system.
if(CMAKE_C_COMPILER_ID MATCHES "Clang" OR CMAKE_C_COMPILER_ID STREQUAL "GNU")
  # These compiler flags can break the checks above so keep them here.
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pedantic -Wall -Wextra -Wshadow \
-Wpointer-arith -Wcast-align -Wwrite-strings \
-Wstrict-prototypes -Wmissing-prototypes \
-Wno-long-long -Wno-format-extra-args")

  if(BUILD_SHARED_LIBS AND UNIX AND NOT APPLE)
    check_linker_flag(C "LINKER:--undefined-version" FLAG_UNDEFINED_VERSION)
    if (FLAG_UNDEFINED_VERSION)
      target_link_options(LibXml2 PRIVATE "LINKER:--undefined-version")
    endif()
    target_link_options(LibXml2 PRIVATE "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/libxml2.syms")
  endif()
endif()

set_target_properties(
    LibXml2
    PROPERTIES
    IMPORT_PREFIX lib
    OUTPUT_NAME xml2
    POSITION_INDEPENDENT_CODE ON
    PREFIX lib
    VERSION ${PROJECT_VERSION}
    SOVERSION ${LIBXML_MAJOR_VERSION}
)

if(MSVC)
    if(BUILD_SHARED_LIBS)
        set_target_properties(
            LibXml2
            PROPERTIES
            DEBUG_POSTFIX d
        )
    else()
        set_target_properties(
            LibXml2
            PROPERTIES
            DEBUG_POSTFIX sd
            MINSIZEREL_POSTFIX s
            RELEASE_POSTFIX s
            RELWITHDEBINFO_POSTFIX s
        )
    endif()
endif()

install(FILES ${LIBXML2_HDRS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/libxml2/libxml COMPONENT development)

install(
    TARGETS    LibXml2
    EXPORT LibXml2
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT development
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT runtime NAMELINK_COMPONENT development
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
)

if(MSVC AND BUILD_SHARED_LIBS)
    install(FILES $<TARGET_PDB_FILE:LibXml2> DESTINATION ${CMAKE_INSTALL_BINDIR} CONFIGURATIONS Debug RelWithDebInfo COMPONENT debug)
endif()
endif()

if(LIBXML2_WITH_PROGRAMS)
    set(
        PROGRAMS
        xmlcatalog
        xmllint
    )
    foreach(PROGRAM ${PROGRAMS})
        add_executable(${PROGRAM} ${PROGRAM}.c)
        add_executable(LibXml2::${PROGRAM} ALIAS ${PROGRAM})
        target_compile_definitions(${PROGRAM} PRIVATE SYSCONFDIR="${CMAKE_INSTALL_FULL_SYSCONFDIR}")
        target_link_libraries(${PROGRAM} LibXml2)
        if(HAVE_LIBHISTORY)
            target_link_libraries(${PROGRAM} history)
        endif()
        if(HAVE_LIBREADLINE)
            target_link_libraries(${PROGRAM} readline)
        endif()
        install(TARGETS    ${PROGRAM} EXPORT LibXml2 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT programs)
    endforeach()
endif()

if(LIBXML2_WITH_TESTS)
    enable_testing()
    set(
        TESTS
        runtest
        runxmlconf
        runsuite
        testapi
        testchar
        testdict
        testModule
        testlimits
        testparser
        testrecurse
        testThreads
    )
    foreach(TEST ${TESTS})
        add_executable(${TEST} ${TEST}.c)
        target_compile_definitions(${TEST} PRIVATE SYSCONFDIR="${CMAKE_INSTALL_FULL_SYSCONFDIR}")
        target_link_libraries(${TEST} LibXml2)
    endforeach()
    if(Threads_FOUND)
        foreach(TEST runtest testThreads)
            target_link_libraries(${TEST} Threads::Threads)
        endforeach()
    endif()
    add_test(NAME runtest COMMAND runtest --out ${CMAKE_CURRENT_BINARY_DIR} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME runsuite COMMAND runsuite WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    if(EXISTS ${LIBXML2_XMLCONF_WORKING_DIR}/xmlconf/xmlconf.xml)
        add_test(NAME runxmlconf COMMAND runxmlconf WORKING_DIRECTORY ${LIBXML2_XMLCONF_WORKING_DIR})
    endif()
    if(NOT WIN32)
        add_test(NAME testapi COMMAND testapi)
    endif()
    add_test(NAME testchar COMMAND testchar)
    add_test(NAME testdict COMMAND testdict)
    add_test(NAME testparser COMMAND testparser WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME testrecurse COMMAND testrecurse WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME testThreads COMMAND testThreads WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

if(LIBXML2_WITH_PYTHON)
    execute_process(
        COMMAND
        ${Python_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/python/generator.py
        ${CMAKE_CURRENT_SOURCE_DIR}/doc/libxml2-api.xml
        ${CMAKE_CURRENT_SOURCE_DIR}/python/libxml2-python-api.xml
        WORKING_DIRECTORY
        ${CMAKE_CURRENT_BINARY_DIR}
    )
    file(READ python/libxml.py LIBXML_PY)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/libxml2.py.in "${LIBXML_PY}")
    file(READ ${CMAKE_CURRENT_BINARY_DIR}/libxml2class.py LIBXML2CLASS_PY)
    file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/libxml2.py.in "${LIBXML2CLASS_PY}")
    configure_file(${CMAKE_CURRENT_BINARY_DIR}/libxml2.py.in libxml2.py COPYONLY)
    add_library(
        LibXml2Mod
        libxml2-py.c
        libxml2-py.h
        python/libxml.c
        python/libxml_wrap.h
        python/types.c
    )
    target_include_directories(
        LibXml2Mod
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/python>
    )
    target_link_libraries(LibXml2Mod LibXml2 Python::Python)
    set_target_properties(
        LibXml2Mod
        PROPERTIES
        IMPORT_PREFIX lib
        OUTPUT_NAME xml2mod
        PREFIX lib
        VERSION ${PROJECT_VERSION}
    )
    if (WIN32)
        set_target_properties(LibXml2Mod PROPERTIES SUFFIX ".pyd")
    endif()
    install(
        TARGETS    LibXml2Mod
        ARCHIVE DESTINATION ${LIBXML2_PYTHON_INSTALL_DIR} COMPONENT development
        LIBRARY DESTINATION ${LIBXML2_PYTHON_INSTALL_DIR} COMPONENT runtime NAMELINK_COMPONENT development
        RUNTIME DESTINATION ${LIBXML2_PYTHON_INSTALL_DIR} COMPONENT runtime
    )
    if(MSVC AND BUILD_SHARED_LIBS)
        install(FILES $<TARGET_PDB_FILE:LibXml2Mod> DESTINATION ${LIBXML2_PYTHON_INSTALL_DIR} CONFIGURATIONS Debug RelWithDebInfo COMPONENT debug)
    endif()
    install(FILES python/drv_libxml2.py DESTINATION ${LIBXML2_PYTHON_INSTALL_DIR} COMPONENT runtime)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libxml2.py DESTINATION ${LIBXML2_PYTHON_INSTALL_DIR} COMPONENT runtime)
endif()

if (FALSE) # XXX(kitware): mask installation rules
install(FILES doc/xml2-config.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1 COMPONENT documentation)
install(FILES doc/xmlcatalog.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1 COMPONENT documentation)
install(FILES doc/xmllint.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1 COMPONENT documentation)
install(DIRECTORY doc/ DESTINATION ${CMAKE_INSTALL_DOCDIR} COMPONENT documentation
    PATTERN "Makefile.*" EXCLUDE
    PATTERN "*.1" EXCLUDE
    PATTERN "*.py" EXCLUDE
    PATTERN "*.res" EXCLUDE
    PATTERN "*.xml" EXCLUDE
    PATTERN "*.xsl" EXCLUDE)

configure_package_config_file(
    libxml2-config.cmake.cmake.in libxml2-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libxml2-${PROJECT_VERSION}
)

install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/libxml2-config.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libxml2-${PROJECT_VERSION}
    COMPONENT development
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/libxml2-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY ExactVersion
)

install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/libxml2-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libxml2-${PROJECT_VERSION}
    COMPONENT development
)

install(
    EXPORT LibXml2
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libxml2-${PROJECT_VERSION}
    NAMESPACE LibXml2::
    FILE libxml2-export.cmake
    COMPONENT development
)
endif ()

if(MSVC)
    configure_file(include/win32config.h include/config.h COPYONLY)
else()
    configure_file(config.h.cmake.in include/config.h)
endif()
configure_file(include/libxml/xmlversion.h.in include/libxml/xmlversion.h)
if (FALSE) # XXX(kitware): mask installation rules
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libxml/xmlversion.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/libxml2/libxml COMPONENT development)
endif ()

if(LIBXML2_WITH_PYTHON)
    set(prefix "${CMAKE_INSTALL_PREFIX}")
    configure_file(python/setup.py.in setup.py @ONLY)
endif()

set(NON_PC_LIBS "${THREAD_LIBS} ${ICONV_LIBS} ${LIBM} ${WINSOCK_LIBS} ${CRYPTO_LIBS}")
list(APPEND XML_PC_LIBS "${NON_PC_LIBS}")
list(APPEND XML_PRIVATE_LIBS "${NON_PC_LIBS}")
list(REMOVE_DUPLICATES XML_PC_LIBS)
list(REMOVE_DUPLICATES XML_PRIVATE_LIBS)

list(JOIN XML_PC_REQUIRES " " XML_PC_REQUIRES)
list(JOIN XML_PC_LIBS " " XML_PC_LIBS)
list(JOIN XML_PRIVATE_LIBS " " XML_PRIVATE_LIBS)

if (FALSE) # XXX(kitware): mask installation rules
set(XML_INCLUDEDIR "-I\${includedir}/libxml2")
set(XML_LIBDIR "-L\${libdir}")
set(XML_LIBS "-lxml2")

if(BUILD_SHARED_LIBS)
    set(XML_PC_PRIVATE ".private")
    set(XML_PC_LIBS_PRIVATE "
Libs.private:")
else()
    target_compile_definitions(LibXml2 PUBLIC LIBXML_STATIC)
    set(XML_CFLAGS "-DLIBXML_STATIC")
    set(XML_PRIVATE_LIBS_NO_SHARED "${XML_PRIVATE_LIBS}")
endif()

file(RELATIVE_PATH PACKAGE_RELATIVE_PATH "${CMAKE_INSTALL_FULL_LIBDIR}/pkgconfig" "${CMAKE_INSTALL_PREFIX}")
string(REGEX REPLACE "/$" "" PACKAGE_RELATIVE_PATH "${PACKAGE_RELATIVE_PATH}")

if(WIN32)
    set(prefix "\${pcfiledir}/${PACKAGE_RELATIVE_PATH}")
else()
    set(prefix "${CMAKE_INSTALL_PREFIX}")
endif()
set(exec_prefix "\${prefix}")
set(libdir "\${prefix}/${CMAKE_INSTALL_LIBDIR}")
set(includedir "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}")
configure_file(libxml-2.0.pc.in libxml-2.0.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libxml-2.0.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig COMPONENT development)

if(WIN32)
    set(prefix "\$(cd \"\$(dirname \"\$0\")\"; pwd -P)/..")
endif()
configure_file(xml2-config.in xml2-config @ONLY)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/xml2-config DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT development)

if(UNIX)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/libxml.m4 DESTINATION ${CMAKE_INSTALL_DATADIR}/aclocal)
endif()

set(XML_INCLUDEDIR "-I${CMAKE_INSTALL_FULL_INCLUDEDIR}/libxml2")
set(XML_LIBDIR "-L${CMAKE_INSTALL_FULL_LIBDIR}")

set(CPACK_COMPONENT_DEVELOPMENT_DEPENDS runtime)
set(CPACK_COMPONENT_PROGRAMS_DEPENDS runtime)
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_DEBIAN_DEVELOPMENT_PACKAGE_DEPENDS "${PACKAGE_TARNAME}")
set(CPACK_DEBIAN_DEVELOPMENT_PACKAGE_NAME "${PACKAGE_TARNAME}-dev")
set(CPACK_DEBIAN_DEVELOPMENT_PACKAGE_SECTION "libdevel")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE ${PACKAGE_URL})
set(CPACK_DEBIAN_PACKAGE_NAME ${PACKAGE_TARNAME})
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PROGRAMS_PACKAGE_DEPENDS "${PACKAGE_TARNAME}")
set(CPACK_DEBIAN_PROGRAMS_PACKAGE_NAME "${PACKAGE_TARNAME}-utils")
set(CPACK_DEBIAN_PROGRAMS_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_RUNTIME_PACKAGE_NAME ${PACKAGE_TARNAME})
set(CPACK_DEBIAN_RUNTIME_PACKAGE_RECOMMENDS "${PACKAGE_TARNAME}-utils")
set(CPACK_DEBIAN_RUNTIME_PACKAGE_SECTION "libs")
set(CPACK_NSIS_PACKAGE_NAME ${PACKAGE_STRING})
set(CPACK_NSIS_URL_INFO_ABOUT ${PACKAGE_URL})
set(CPACK_PACKAGE_DISPLAY_NAME ${PACKAGE_STRING})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "${PACKAGE_TARNAME}-${PACKAGE_VERSION}")
set(CPACK_PACKAGE_NAME ${PACKAGE_TARNAME})
set(CPACK_PACKAGE_VERSION ${PACKAGE_VERSION})
set(CPACK_PACKAGE_VERSION_MAJOR ${LIBXML_MAJOR_VERSION})
set(CPACK_PACKAGE_VERSION_MINOR ${LIBXML_MINOR_VERSION})
set(CPACK_PACKAGE_VERSION_PATCH ${LIBXML_MICRO_VERSION})
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/Copyright)
set(CPACK_RPM_COMPONENT_INSTALL ON)
set(CPACK_RPM_development_PACKAGE_NAME "${PACKAGE_NAME}-devel")
set(CPACK_RPM_development_PACKAGE_REQUIRES "${PACKAGE_NAME}")
set(CPACK_RPM_PACKAGE_GROUP "Development/Libraries")
set(CPACK_RPM_PACKAGE_NAME ${PACKAGE_TARNAME})
set(CPACK_RPM_PACKAGE_URL ${PACKAGE_URL})
set(CPACK_RPM_programs_PACKAGE_NAME "${PACKAGE_NAME}-utils")
set(CPACK_RPM_programs_PACKAGE_REQUIRES "${PACKAGE_NAME}")
set(CPACK_RPM_runtime_PACKAGE_NAME "${PACKAGE_NAME}")
set(CPACK_RPM_runtime_PACKAGE_SUGGESTS "${PACKAGE_NAME}-utils")

include(CPack)
endif ()
