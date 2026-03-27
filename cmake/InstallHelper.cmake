#
# InstallHelper.cmake
# Post-install scripting: config files, launcher, systemd service setup.
# Called from the top-level CMakeLists install(SCRIPT ...).
#

set(OME_INSTALL_DIR /usr/share/ovenmediaengine)
set(OME_CONF_DIR    ${OME_INSTALL_DIR}/conf)
set(OME_LINK_BIN    /usr/bin/OvenMediaEngine)
set(OME_SERVICE_DIR /lib/systemd/system)
set(OME_SERVICE_LINK /etc/systemd/system/ovenmediaengine.service)

# Determine source root (misc/ directory)
get_filename_component(SRC_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(MISC_DIR ${SRC_ROOT}/misc)

# Create directories
file(MAKE_DIRECTORY ${OME_CONF_DIR})

# Install launcher script
if(EXISTS "${MISC_DIR}/ome_launcher.sh")
    execute_process(COMMAND install -m 755 ${MISC_DIR}/ome_launcher.sh ${OME_INSTALL_DIR})
endif()

# Install default config (skip if already present)
foreach(cfg Server.xml Logger.xml)
    set(_dst ${OME_CONF_DIR}/${cfg})
    if(NOT EXISTS "${_dst}")
        message(STATUS "[OME Install] Installing default config: ${_dst}")
        execute_process(COMMAND install -m 644 ${MISC_DIR}/conf_examples/${cfg} ${_dst})
    else()
        message(STATUS "[OME Install] Skipping existing config: ${_dst}")
    endif()
endforeach()

# Create symlink for binary
if(EXISTS "${OME_INSTALL_DIR}/OvenMediaEngine")
    execute_process(COMMAND ln -sf ${OME_INSTALL_DIR}/OvenMediaEngine ${OME_LINK_BIN})
endif()

# Install systemd service
set(_service ${OME_SERVICE_DIR}/ovenmediaengine.service)
if(NOT EXISTS "${_service}")
    execute_process(COMMAND install -m 644 ${MISC_DIR}/ovenmediaengine.service ${OME_SERVICE_DIR})
    execute_process(COMMAND ln -sf ${_service} ${OME_SERVICE_LINK})
else()
    message(STATUS "[OME Install] Skipping existing service: ${_service}")
endif()
