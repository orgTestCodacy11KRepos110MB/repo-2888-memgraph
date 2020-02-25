# Install the license file.
if (MG_ENTERPRISE)
  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE_ENTERPRISE_TRIAL.md
          DESTINATION share/doc/memgraph RENAME copyright)
else()
  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE_COMMUNITY.md
          DESTINATION share/doc/memgraph RENAME copyright)
endif()

# Install systemd service (must use absolute path).
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/memgraph.service
        DESTINATION /lib/systemd/system)

# Install examples
set(examples ${CMAKE_CURRENT_SOURCE_DIR}/examples)
install(
  CODE
  "execute_process(COMMAND ${examples}/build_examples
                   ${CMAKE_BINARY_DIR}/memgraph
                   ${CMAKE_BINARY_DIR}/tools/src/mg_client
                   WORKING_DIRECTORY ${examples})")
install(DIRECTORY ${examples}/build/ DESTINATION share/memgraph/examples)

# ---- Setup CPack --------

# General setup
set(CPACK_PACKAGE_NAME memgraph)
set(CPACK_PACKAGE_VENDOR "Memgraph Ltd.")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "High performance, in-memory, transactional graph database")

# DEB specific
# Instead of using "name <email>" format, we use "email (name)" to prevent
# errors due to full stop, '.' at the end of "Ltd". (See: RFC 822)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "tech@memgraph.com (Memgraph Ltd.)")
set(CPACK_DEBIAN_PACKAGE_SECTION non-free/database)
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE https://memgraph.com)
set(CPACK_DEBIAN_PACKAGE_VERSION "${MEMGRAPH_VERSION_DEB}")
set(CPACK_DEBIAN_FILE_NAME "memgraph_${MEMGRAPH_VERSION_DEB}_amd64.deb")
if (MG_ENTERPRISE)
  set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
      "${CMAKE_CURRENT_SOURCE_DIR}/debian/enterprise/conffiles;"
      "${CMAKE_CURRENT_SOURCE_DIR}/debian/enterprise/copyright;"
      "${CMAKE_CURRENT_SOURCE_DIR}/debian/prerm;"
      "${CMAKE_CURRENT_SOURCE_DIR}/debian/postrm;"
      "${CMAKE_CURRENT_SOURCE_DIR}/debian/postinst;")
else()
  set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
      "${CMAKE_CURRENT_SOURCE_DIR}/debian/community/conffiles;"
      "${CMAKE_CURRENT_SOURCE_DIR}/debian/community/copyright;"
      "${CMAKE_CURRENT_SOURCE_DIR}/debian/prerm;"
      "${CMAKE_CURRENT_SOURCE_DIR}/debian/postrm;"
      "${CMAKE_CURRENT_SOURCE_DIR}/debian/postinst;")
endif()
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# Description formatting is important, summary must be followed with a newline and 1 space.
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION_SUMMARY}
 Contains Memgraph, the graph database. It aims to deliver developers the
 speed, simplicity and scale required to build the next generation of
 applications driver by real-time connected data.")
# Add `openssl` package to dependencies list. Used to generate SSL certificates.
set(CPACK_DEBIAN_PACKAGE_DEPENDS "openssl (>= 1.1.0)")

# RPM specific
set(CPACK_RPM_PACKAGE_URL https://memgraph.com)
set(CPACK_RPM_PACKAGE_VERSION "${MEMGRAPH_VERSION_RPM}")
set(CPACK_RPM_FILE_NAME "memgraph_${MEMGRAPH_VERSION_RPM}.x86_64.rpm")
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
  /var /var/lib /var/log /etc/logrotate.d
  /lib /lib/systemd /lib/systemd/system /lib/systemd/system/memgraph.service)
set(CPACK_RPM_PACKAGE_REQUIRES_PRE "shadow-utils")
if (ME_ENTERPRISE)
  set(CPACK_RPM_USER_BINARY_SPECFILE "${CMAKE_CURRENT_SOURCE_DIR}/rpm/enterprise/memgraph.spec.in")
  set(CPACK_RPM_PACKAGE_LICENSE "Memgraph Enterprise Trial License")
else()
  set(CPACK_RPM_USER_BINARY_SPECFILE "${CMAKE_CURRENT_SOURCE_DIR}/rpm/community/memgraph.spec.in")
  set(CPACK_RPM_PACKAGE_LICENSE "Memgraph Community License")
endif()
# Description formatting is important, no line must be greater than 80 characters.
set(CPACK_RPM_PACKAGE_DESCRIPTION "Contains Memgraph, the graph database.
It aims to deliver developers the speed, simplicity and scale required to build
the next generation of applications driver by real-time connected data.")
# Add `openssl` package to dependencies list. Used to generate SSL certificates.
set(CPACK_RPM_PACKAGE_REQUIRES "openssl >= 1.0.0, curl >= 7.29.0")

# All variables must be set before including.
include(CPack)