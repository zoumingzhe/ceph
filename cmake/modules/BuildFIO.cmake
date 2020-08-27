function(build_fio)
  # we use an external project and copy the sources to bin directory to ensure
  # that object files are built outside of the source tree.
  include(ExternalProject)
  if(ALLOCATOR)
    set(FIO_EXTLIBS EXTLIBS=-l${ALLOCATOR})
  endif()
  ExternalProject_Add(fio_ext
    DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/src/
    UPDATE_COMMAND "" # this disables rebuild on each run
    GIT_REPOSITORY "https://github.com/axboe/fio.git"
    GIT_CONFIG advice.detachedHead=false
    GIT_SHALLOW 1
    GIT_TAG "fio-3.15"
    SOURCE_DIR ${CMAKE_BINARY_DIR}/src/fio
    BUILD_IN_SOURCE 1
    CONFIGURE_COMMAND <SOURCE_DIR>/configure
    BUILD_COMMAND $(MAKE) fio EXTFLAGS=-Wno-format-truncation ${FIO_EXTLIBS}
    INSTALL_COMMAND cp <BINARY_DIR>/fio ${CMAKE_BINARY_DIR}/bin)
endfunction()
