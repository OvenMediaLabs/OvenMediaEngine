#!/bin/bash
#==============================================================================
#
#  OvenMediaEngine Launcher
#
#  Created by Hyunjun Jang
#  Copyright (c) 2023 AirenSoft. All rights reserved.
#
#==============================================================================
VERSION="v0.4"

prepare_colors()
{
	local COLORS=$(tput colors 2>/dev/null)

	if [[ -n "${COLORS}" && ${COLORS} -ge 8 ]]
	then
		COLOR_BOLD="$(tput bold)"
		COLOR_UNDERLINE="$(tput smul)"
		COLOR_STANDOUT="$(tput smso)"
		COLOR_RESET="$(tput sgr0)"

		COLOR_BLACK="$(tput setaf 0)"
		COLOR_RED="$(tput setaf 1)"
		COLOR_GREEN="$(tput setaf 2)"
		COLOR_YELLOW="$(tput setaf 3)"
		COLOR_BLUE="$(tput setaf 4)"
		COLOR_MAGENTA="$(tput setaf 5)"
		COLOR_CYAN="$(tput setaf 6)"
		COLOR_WHITE="$(tput setaf 7)"
		COLOR_GRAY="$(tput setaf 8)"
	fi

	PRESET_HIGHLIGHT="${COLOR_CYAN}"
}

logd()
{
	if ! ${DEBUG}
	then
		return
	fi

	local ECHO_OPTIONS=()

	while [ $# -gt 0 ]
	do
		local ARG="$1"
		[[ "${ARG}" == -* ]] || break
		
		ECHO_OPTIONS+=("${ARG}")
		shift
	done

	local ARGS=("$@")
	echo "${ECHO_OPTIONS[@]}" "${COLOR_CYAN}${ARGS[@]}${COLOR_RESET}"
}

logi()
{
	local ECHO_OPTIONS=()

	while [ $# -gt 0 ]
	do
		local ARG="$1"
		[[ "${ARG}" == -* ]] || break
		
		ECHO_OPTIONS+=("${ARG}")
		shift
	done

	local ARGS=("$@")
	echo "${ECHO_OPTIONS[@]}" "${ARGS[@]}${COLOR_RESET}"
}

logw()
{
	local ECHO_OPTIONS=()

	while [ $# -gt 0 ]
	do
		local ARG="$1"
		[[ "${ARG}" == -* ]] || break
		
		ECHO_OPTIONS+=("${ARG}")
		shift
	done

	local ARGS=("$@")
	echo "${ECHO_OPTIONS[@]}" "${COLOR_YELLOW}${ARGS[@]}${COLOR_RESET}"
}

loge()
{
	local ECHO_OPTIONS=()

	while [ $# -gt 0 ]
	do
		local ARG="$1"
		[[ "${ARG}" == -* ]] || break
		
		ECHO_OPTIONS+=("${ARG}")
		shift
	done

	local ARGS=("$@")
	echo "${ECHO_OPTIONS[@]}" "${COLOR_RED}${ARGS[@]}${COLOR_RESET}"
}

die()
{
	local ARGS=("$@")
	loge "${ARGS[@]}"
	exit 1
}

need_to_report()
{
	local ARGS=("$@")
	loge "• ERROR: ${ARGS[@]}"
	loge "• This may be a bug of this launcher, and should not be occurred."
	loge "• Please report this issue to https://github.com/AirenSoft/OvenMediaEngine/issues"
	exit 1
}

banner()
{
	${HIDE_BANNER} && return

	logi
	logi "${COLOR_YELLOW} ▄██████▀███▄ "
	logi "${COLOR_YELLOW}█████▀ ▄██████${COLOR_RESET}  ${COLOR_YELLOW}OvenMediaEngine${COLOR_RESET} Launcher ${VERSION}"
	logi "${COLOR_YELLOW}███▄▄▄▄▀▀▀▀███"
	logi "${COLOR_YELLOW}██████▀ ▄█████${COLOR_RESET}  ${COLOR_BLUE}https://github.com/AirenSoft/OvenMediaEngine"
	logi "${COLOR_YELLOW} ▀███▄██████▀ "
	logi
}

# Configurations
IMAGE_NAME=airensoft/ovenmediaengine:latest
CONTAINER_NAME=${CONTAINER_NAME:-ovenemediaengine}
PREFIX=${PREFIX:-/usr/share/ovenmediaengine/}

if command -v realpath > /dev/null 2>&1
then
	PREFIX=$(realpath "${PREFIX}")/
else
	if command -v readlink > /dev/null 2>&1
	then
		PREFIX=$(readlink -f "$(dirname ${PREFIX})")/$(basename "${PREFIX}")/
	else
		[ -z "${DOCKER}" ] && die "• ERROR: realpath/readlink not found"
	fi
fi

CONF_PATH=${PREFIX}conf
LOGS_PATH=${PREFIX}logs
CRASH_DUMPS_PATH=${PREFIX}dumps
OME_MEDIA_ROOT=${OME_MEDIA_ROOT:-/dev/null}

SERVER_XML_PATH="${CONF_PATH}/Server.xml"
LOGGER_XML_PATH="${CONF_PATH}/Logger.xml"

DOCKER=${DOCKER:-$(which docker)}

run()
{
	local ARGS=("$@")
	local TARGET="$(basename "${ARGS[0]}")"

	if ${DEBUG}
	then
		logi " ${COLOR_GRAY}┌── ${COLOR_CYAN}${ARGS[@]}"

		# Colorize stdout/stderr messages
		set -o pipefail;
		"$@" \
			2> >(sed "s,.*,   ${COLOR_GRAY}${TARGET}> ${COLOR_RED}&${COLOR_RESET},">&2;) \
			1> >(sed "s,.*,   ${COLOR_GRAY}${TARGET}> ${COLOR_GRAY}&${COLOR_RESET},">&1;)
		local EXIT_CODE=$?

		logi -n " ${COLOR_GRAY}└── "

		if [ ${EXIT_CODE} -eq 0 ]
		then
			logi "${COLOR_GRAY}Succeeded"
		else
			logi "${COLOR_RED}Failed: ${EXIT_CODE}"
		fi
	else
		# Colorize stdout/stderr messages
		set -o pipefail;
		"$@" \
			2> >(sed "s,.*,  ${COLOR_GRAY}${TARGET}> ${COLOR_RED}&${COLOR_RESET},">&2;) \
			1> >(sed "s,.*,  ${COLOR_GRAY}${TARGET}> ${COLOR_GRAY}&${COLOR_RESET},">&1;)
		local EXIT_CODE=$?

		[ ${EXIT_CODE} -ne 0 ] && loge "  ${COLOR_RED}(Failed: ${EXIT_CODE})"
	fi

	return ${EXIT_CODE}
}

check_docker_is_available()
{
	[ -z "${DOCKER}" ] && die "• ERROR: Docker not found"
	command -v "${DOCKER}" >/dev/null 2>&1 && return

	loge "• Could not run ${DOCKER}"
	exit 1
}

# Check if the configuration files already exist
#
# Returns 0 if the configuration files already exist, otherwise 1
check_already_setup()
{
	[ -f "${SERVER_XML_PATH}" ] && return 0
	[ -f "${LOGGER_XML_PATH}" ] && return 0

	return 1
}

# Get the container ID
get_container_id()
{
	"${DOCKER}" ps -a -q -f name="${CONTAINER_NAME}" 2>/dev/null
}

# Check if the container is running
#
# Returns 0 if the container is running, otherwise 1
is_container_running()
{
	[ ! -z "$(get_container_id)" ]
}

die_if_container_not_running()
{
	is_container_running && return

	loge "• ERROR: Container is not running"
	logi
	logi "• If you want to start OvenMediaEngine container, please run ${PRESET_HIGHLIGHT}${0} start"
	die
}

_setup()
{
	prepare_dir_if_needed()
	{
		if [ ! -d "${CONF_PATH}" ]
		then
			logi "• Creating configuration directory ${PRESET_HIGHLIGHT}${CONF_PATH}"
			run mkdir -p "${CONF_PATH}"
		fi
	}

	pull_image()
	{
		logi "• Pulling ${PRESET_HIGHLIGHT}${IMAGE_NAME}"
		run "${DOCKER}" pull "${IMAGE_NAME}" || die "• ERROR: Could not pull the latest image"
	}

	copy_config_files()
	{
		local DO_COPY=false

		# Check if the configuration file exists
		if check_already_setup
		then
			logw "• Configuration files already exist in ${PRESET_HIGHLIGHT}${CONF_PATH}"

			while true
			do
				logw -n "• Do you want to overwrite them? (y/N) "
				local ANSWER
				read -r ANSWER

				case "${ANSWER}" in
					y|Y)
						ANSWER="y"
						break
						;;
					n|N)
						ANSWER="n"
						break
						;;
					*)
				esac
			done

			if [ "${ANSWER}" == "y" ]
			then
				local BAK_CONF_PATH="${CONF_PATH}_$(date +%Y%m%d%H%M%S)"
				DO_COPY=true
				logi "• Backing up configuration files to ${PRESET_HIGHLIGHT}${BAK_CONF_PATH}"
				{
					run mkdir -p "${BAK_CONF_PATH}" &&
					run mv "${CONF_PATH}"/* "${BAK_CONF_PATH}"
				} || die "• ERROR: Could not create backup"
			else
				logi "• OvenMediaEngine will use the existing configuration files"
			fi
		else
			DO_COPY=true
		fi

		if ${DO_COPY}
		then
			logi "• Copying configuration to ${PRESET_HIGHLIGHT}${CONF_PATH}" &&
			run "${DOCKER}" run \
				--rm \
				--mount=type=bind,source="${CONF_PATH}",target=/tmp/conf \
				"${IMAGE_NAME}" \
				/bin/bash -c 'cp /opt/ovenmediaengine/bin/origin_conf/* /tmp/conf/'
		fi
	}

	{
		prepare_dir_if_needed &&
		copy_config_files
	} || die "• ERROR: Could not copy configuration file"
	
	if [ ! -d "${LOGS_PATH}" ]
	then
		logi "• Creating logs directory"
		run mkdir -p "${LOGS_PATH}" || { loge "• Could not create logs directory"; exit 1; }
	fi

	if [ ! -d "${CRASH_DUMPS_PATH}" ]
	then
		logi "• Creating crash dump directory"
		run mkdir -p "${CRASH_DUMPS_PATH}" || { loge "• Could not create crash dump directory"; exit 1; }
	fi

	HIDE_POST_SETUP_MESSAGE=${HIDE_POST_SETUP_MESSAGE:-false}

	if ! ${HIDE_POST_SETUP_MESSAGE}
	then
		logi "• ${COLOR_GREEN}OvenMediaEngine is ready to start!"
		logi
		logi "If you want to change the settings, please modify ${PRESET_HIGHLIGHT}${CONF_PATH}/Server.xml"
		logi "If you want to start OvenMediaEngine, please run ${PRESET_HIGHLIGHT}${0} start"
	fi
}

_start()
{
	if is_container_running
	then
		loge "• ERROR: OvenMediaEngine container is already running"
		logi
		logi "• If you want to stop OvenMediaEngine container, please run ${PRESET_HIGHLIGHT}${0} stop"
		die
	fi

	if ! check_already_setup
	then
		logw "• OvenMediaEngine container is not set up yet. Setting up..."
		HIDE_POST_SETUP_MESSAGE=true
		_setup
	fi

	logi "• Starting OvenMediaEngine..."

	local DEBUG_AWK=0
	${DEBUG} && DEBUG_AWK=1

	logi "• Obtaining the port list from ${PRESET_HIGHLIGHT}${SERVER_XML_PATH}"

	# Obtain the used port list
	local BIND_LIST=$(cat "${SERVER_XML_PATH}" | \
		# Remove the XML comments
		awk '
			# Skip the characters until the end of the comment (-->)
			IS_COMMENT && /-->/ { sub( /.*-->/, "" ); IS_COMMENT = 0 }

			# Skip the line if it is a comment
			IS_COMMENT { next }

			# Delete single line comments
			{ gsub( /<!--.*-->/, "" ) }

			# Find the start of a multi-line comment
			{ IS_COMMENT = sub( /<!--.*/, "" ); print }
		' | \
		# Get the port list
		awk -v ORS='' \
			-F '>' '

			BEGIN {
				DEBUG = '${DEBUG_AWK}'
				IS_BIND = 0
				IS_FINISHED = 0
				IS_PORT = 0
				INDENT = ""

				MODULE_INDEX = 0
				PARENT_INDEX = 0

				COLOR_BLACK="\033[30m"
				COLOR_RED="\033[31m"
				COLOR_GREEN="\033[32m"
				COLOR_YELLOW="\033[33m"
				COLOR_BLUE="\033[34m"
				COLOR_MAGENTA="\033[35m"
				COLOR_CYAN="\033[36m"
				COLOR_WHITE="\033[37m"
				COLOR_RESET="\033[0m"
			}

			{
				if( IS_FINISHED ) {
					next
				}

				# Remove the leading/trailing spaces
				gsub( /^[ \t]+/, "" )
				gsub( /[ \t]+$/, "" )
			}

			function logd( MESSAGE ) {
				if( DEBUG ) {
					print INDENT COLOR_CYAN MESSAGE COLOR_RESET > "/dev/stderr"
				}
			}

			function logd_noindent( MESSAGE ) {
				if( DEBUG ) {
					print COLOR_CYAN MESSAGE COLOR_RESET > "/dev/stderr"
				}
			}

			length($0) > 0 {
				logd_noindent( "Processing a line: [" $0 "]\n" )
				for( INDEX = 1; INDEX <= NF; INDEX++ ) {
					logd_noindent( "  - FIELD[" INDEX "] = [" $INDEX "], " )
				}
				logd_noindent( "\n" )

				for( INDEX = 1; INDEX <= NF; INDEX++ ) {
					if( $INDEX ~ /<([^ ]+)/ ) {
						START_INDEX = index($INDEX, "<") + 1
						END_INDEX = index(substr($INDEX, START_INDEX), " ") - 1
						if (END_INDEX == -1) {
							# No space found, so the tag ends at the next ">"
							END_INDEX = length($INDEX) - START_INDEX + 1
						}
						TAG_NAME = substr($INDEX, START_INDEX, END_INDEX)

						if( !IS_BIND ) {
							if( TAG_NAME == "Bind" ) {
								IS_BIND = 1
							} else {
								if( TAG_NAME !~ /^\// ) {
									logd( "  => <" TAG_NAME "> is not <Bind>. Skipping...\n" )
								}
								continue
							}
						}

						if( TAG_NAME ~ /^?xml/ ) {
							# Ignore the XML definition
						}
						else if( TAG_NAME ~ /^(Port|TLSPort|IceCandidate|TcpRelay)$/ ) {
							# Tag is one of the port tags, <Port>, <TLSPort>, <IceCandidate>, <TcpRelay>

							# Get the port number from the element
							logd( COLOR_BLUE "<" TAG_NAME "> " COLOR_GREEN "// Port, Parent: " PARENTS[PARENT_INDEX] ", Module: " MODULES[MODULE_INDEX] "\n" )
							PARENTS[++PARENT_INDEX] = TAG_NAME
							INDENT = INDENT "  "
							IS_PORT = 1
						}
						else if( TAG_NAME ~ /^\// ) {
							# Close tag found
							CURRENT_PARENT = PARENTS[PARENT_INDEX]

							if( IS_PORT ) {
								# C-DATA

								# Find the position of the closing tag
								CLOSE_INDEX = index($INDEX, "</")
								if( CLOSE_INDEX > 0 ) {
									# Extract content before the closing tag
									REMAINED = substr($INDEX, 1, CLOSE_INDEX - 1)
								} else {
									# No closing tag found, take the entire content
									REMAINED = $INDEX
								}

								if( length(REMAINED) > 0 ) {
									logd( REMAINED COLOR_GREEN " // C-DATA before close tag, " COLOR_BLUE PARENTS[PARENT_INDEX - 1] COLOR_RESET "/" COLOR_BLUE PARENTS[PARENT_INDEX] ", Module: " MODULES[MODULE_INDEX] "\n" )

									print "PORT|" MODULES[MODULE_INDEX - 1] "|" MODULES[MODULE_INDEX] "|" CURRENT_PARENT "|" REMAINED "\n"
								}
							}

							IS_PORT = 0

							PARENT_INDEX--

							CURRENT_PARENT = PARENTS[PARENT_INDEX]

							if( ( CURRENT_PARENT == "Providers" ) ||
								( CURRENT_PARENT == "Publishers" ) ||
								( CURRENT_PARENT == "Managers" ) ||
								( TAG_NAME == "Providers" ) ||
								( TAG_NAME == "Publishers" ) ||
								( TAG_NAME == "Managers" ) ) {
								MODULE_INDEX--
							}
							
							INDENT = substr(INDENT, 1, length(INDENT) - 2)

							logd( COLOR_BLUE "<" TAG_NAME "> " COLOR_GREEN " // Close, Parent: " CURRENT_PARENT ", Module: " MODULES[MODULE_INDEX] "\n" )

							if( TAG_NAME == "/Bind" ) {
								logd_noindent( "</Bind> found. Stopping...\n" )

								IS_FINISHED = 1
							}
						}
						else {
							CURRENT_PARENT = PARENTS[PARENT_INDEX]

							# Open tag found
							if( $INDEX ~ /\/$/ ) {
								# Self-closing tag found
								logd(COLOR_BLUE "<" TAG_NAME " />" COLOR_GREEN " // Self-closed, Parent: " CURRENT_PARENT ", Module: " MODULES[MODULE_INDEX] "\n" )
							} else {
								# Traverse the children
								PARENTS[++PARENT_INDEX]=TAG_NAME

								logd(COLOR_BLUE "<" TAG_NAME ">" COLOR_GREEN " // Open, Parent: " PARENTS[PARENT_INDEX - 1] ", Module: " MODULES[MODULE_INDEX] "\n" )
								INDENT = INDENT "  "

								if( ( CURRENT_PARENT == "Providers" ) ||
									( CURRENT_PARENT == "Publishers" ) ||
									( CURRENT_PARENT == "Managers" ) ||
									( TAG_NAME == "Providers" ) ||
									( TAG_NAME == "Publishers" ) ||
									( TAG_NAME == "Managers" ) ) {
									MODULES[++MODULE_INDEX] = TAG_NAME
								}
							}

							if( ( CURRENT_PARENT == "Providers" ) ||
								( CURRENT_PARENT == "Publishers" ) ||
								( CURRENT_PARENT == "Managers" ) ) {
								print "MODULE|" CURRENT_PARENT "|" TAG_NAME "\n"
							}
						}
					}
					else if( length($INDEX) > 0 ) {
						# C-DATA
						if( IS_PORT ) {
							logd( $INDEX COLOR_GREEN " // C-DATA, " COLOR_BLUE PARENTS[PARENT_INDEX - 1] COLOR_RESET "/" COLOR_BLUE PARENTS[PARENT_INDEX] "\n" )
							print "PORT|" MODULES[MODULE_INDEX - 1] "|" MODULES[MODULE_INDEX] "|" PARENTS[PARENT_INDEX] "|" $INDEX "\n"
						}
					}
				}
			}
		')

	if [ "${BIND_LIST[@]}" = '' ]
	then
		loge "• There is no Bind list. Please check the settings."
		exit 1
	fi

	# Above code is to parse the XML file and extract the port information, such as:
	#
	# MODULE|Providers|RTMP
	# MODULE|Providers|SRT
	# PORT|Providers|RTMP|Port|${env:OME_RTMP_PROV_PORT:1935}
	# PORT|Providers|WebRTC|Port|${env:OME_WEBRTC_SIGNALLING_PORT:3333}
	# ...
	PORT_LIST=()
	MODULE_LIST=()

	PROVIDER_LIST=()
	PUBLISHER_LIST=()

	# Extract the module list first because the port may not be specified, such as <RTMP />
	for ITEM in ${BIND_LIST[@]}
	do
		# MODULE|Providers|RTMP
		# ~~~~~~ ~~~~~~~~~ ~~~~
		#   ^        ^      ^
		#   |        |      +---- NAME (RTMP, SRT, WebRTC, ...)
		#   |        +---- TYPE (Providers, Publishers)
		#   +---- ITEM_TYPE (PORT/MODULE)
		local ITEM_TYPE=${ITEM%%|*}

		case "${ITEM_TYPE}" in
			MODULE) MODULE_LIST+=("${ITEM#*|}") ;;
			PORT) ;;
			*) need_to_report "Not supported item type: ${ITEM_TYPE}" ;;
		esac
	done

	# Show the module list if debug mode is enabled
	if ${DEBUG}
	then
		if [ ${#MODULE_LIST[@]} -gt 0 ]
		then
			logd "• Modules are enabled"

			for MODULE in ${MODULE_LIST[@]}
			do
				logd "  - ${MODULE}"
			done
		fi
	fi

	# Extract the port list from BIND_LIST
	for ITEM in ${BIND_LIST[@]}
	do
		local ITEM_TYPE=${ITEM%%|*}

		REMAINED=${ITEM#*|}

		if [ ! "${ITEM_TYPE}" = "PORT" ]
		then
			continue
		fi

		# PORT|Providers|RTMP|Port|${env:OME_RTMP_PROV_PORT:1935}
		# ~~~~ ~~~~~~~~~ ~~~~ ~~~~ ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
		#   ^     ^        ^    ^               ^
		#   |     |        |    |               +---- VALUE
		#   |     |        |    +---- PORT_TYPE (Port, TLSPort, IceCandidate, TcpRelay)
		#   |     |        +---- NAME (RTMP, SRT, WebRTC, ...)
		#   |     +---- TYPE (Providers, Publishers)
		#   +---- ITEM_TYPE (PORT/MODULE)
		local TYPE=${REMAINED%%|*}
		local NAME=${REMAINED#*|}
		NAME=${NAME%%|*}
		local PORT_TYPE=${REMAINED#*|}
		PORT_TYPE=${PORT_TYPE#*|}
		PORT_TYPE=${PORT_TYPE%%|*}
		TYPE=${TYPE%%|*}
		local VALUE=${REMAINED##*|}

		# Change ${env:OME_RTMP_PROV_PORT:1935} to ${OME_RTMP_PROV_PORT:-1935}, and then evaluate it
		VALUE=$(eval echo "$(echo "${VALUE}" | sed -E 's/\$\{env:([^:]+)(:([^}]+))?\}/\${\1:-\3}/g')")

		local DESCRIPTION=

		case "${TYPE}" in
			Providers) DESCRIPTION="${NAME} Provider" ;;
			Publishers) DESCRIPTION="${NAME} Publisher" ;;
			Managers) DESCRIPTION="${NAME} (Manager)" ;;
			*)
				# Above awk command should not generate this case
				need_to_report "Not supported type: ${TYPE}"
				;;
		esac

		# Parse [<IP>:]<START>[-<END>][/<PROTOCOL>] format
		{
			[[ ! "${VALUE}" =~ ^(\[?([^\]]+)\]?:)?((([0-9]+)(-([0-9]+))?)(,(([0-9]+)(-([0-9]+))?))*)(\/(.+))?$ ]] &&
			die "• ERROR: Invalid setting found: '${VALUE}' in ${TYPE}/${NAME}" ;
		}

		local IP="${BASH_REMATCH[2]}"
		local PORT_RANGE="${BASH_REMATCH[3]}"
		local PROTOCOL="${BASH_REMATCH[14]}"
		PROTOCOL=$(echo -n "${PROTOCOL}" | tr '[:lower:]' '[:upper:]')

		# Some protocol uses UDP by default (#1339)
		case "${NAME}" in
			SRT) PROTOCOL=${PROTOCOL:-SRT} ;;
		esac

		# SRT actually uses UDP, so bind UDP port if SRT is specified
		[ "${PROTOCOL}" = "SRT" ] && PROTOCOL="UDP"
		[ ! -z "${PROTOCOL}" ] && PROTOCOL="/${PROTOCOL}"

		# Support multiple port ranges (e.g. 1935,1937-1940)
		local PORT_RANGE_LIST=()
		IFS=',' read -r -a PORT_RANGE_LIST <<< "${PORT_RANGE}"
		local USED_PORT_LIST=
		for PORT_RANGE in "${PORT_RANGE_LIST[@]}"
		do
			local START_PORT=${PORT_RANGE%%-*}
			local END_PORT=

			[[ "$PORT_RANGE" == *"-"* ]] && END_PORT=${PORT_RANGE##*-}
			[ ! -z "${END_PORT}" ] && END_PORT="-${END_PORT}"

			PORT_LIST+=("-p ${START_PORT}${END_PORT}:${START_PORT}${END_PORT}${PROTOCOL}")
			
			[ ! -z "${USED_PORT_LIST}" ] && USED_PORT_LIST="${USED_PORT_LIST}, "
			USED_PORT_LIST="${USED_PORT_LIST}${START_PORT}${END_PORT}${PROTOCOL}"
		done

		logi "  - ${COLOR_GREEN}${DESCRIPTION}${COLOR_RESET} is configured to use ${COLOR_MAGENTA}${USED_PORT_LIST}${COLOR_RESET} (${PORT_TYPE})"

		for INDEX in "${!MODULE_LIST[@]}"
		do
			local MODULE="${MODULE_LIST[${INDEX}]}"

			if [ "${MODULE}" = "${TYPE}|${NAME}" ]
			then
				# Remove the module from the list
				logd "    - Do not need to use default port range for module ${MODULE}"
				unset MODULE_LIST[${INDEX}]
				break
			fi
		done
	done

	# Use default port range for modules that are not specified in the PORT_LIST
	for MODULE in ${MODULE_LIST[@]}
	do
		# Providers|RTMP
		# ~~~~~~~~~ ~~~~
		#   ^        ^
		#   |        +---- NAME (RTMP, SRT, WebRTC, ...)
		#   +---- TYPE (Providers, Publishers)
		local TYPE=${MODULE%%|*}
		local NAME=${MODULE#*|}

		local DESCRIPTION=

		case "${TYPE}" in
			Providers) DESCRIPTION="${NAME} Provider" ;;
			Publishers) DESCRIPTION="${NAME} Publisher" ;;
		esac

		local PORT=
		local PROTOCOL=

		case "${NAME}" in
			RTMP) PORT=1935; ;;
			SRT) PORT=9999; PROTOCOL=udp ;;
			WebRTC) PORT=10000; PROTOCOL=udp ;;
			RTSPC) continue ;;
			OVT) continue ;;
			*) need_to_report "Not supported module: ${NAME}" ;;
		esac
		PROTOCOL=${PROTOCOL^^}
		[ ! -z "${PROTOCOL}" ] && PROTOCOL="/${PROTOCOL}"

		logi "  - ${COLOR_GREEN}${DESCRIPTION}${COLOR_RESET} will use ${COLOR_YELLOW}DEFAULT PORT${COLOR_RESET} ${COLOR_MAGENTA}${PORT}${PROTOCOL}"
		PORT_LIST+=("-p ${PORT}:${PORT}${PROTOCOL}")
	done

	# Remove duplicated ports
	PORT_LIST=$(printf '%s\n' "${PORT_LIST[@]}" | sort | uniq)

	if [ ! -z "${IP_ADDRESS}" ]
	then
		logi "• Use ${COLOR_MAGENTA}${IP_ADDRESS}${COLOR_RESET} as the ICE candidate host"
		export OME_HOST_IP="${IP_ADDRESS}"
		export OME_WEBRTC_CANDIDATE_IP="${IP_ADDRESS}"
	fi

	logi "• Starting a container: ${PRESET_HIGHLIGHT}${CONTAINER_NAME}"
	
	run "${DOCKER}" run -d \
		${PORT_LIST} \
		-e OME_HOST_IP \
		-e OME_WEBRTC_CANDIDATE_IP \
		-e OME_ORIGIN_PORT \
		-e OME_RTMP_PROV_PORT \
		-e OME_SRT_PROV_PORT \
		-e OME_MPEGTS_PROV_PORT \
		-e OME_LLHLS_STREAM_PORT \
		-e OME_LLHLS_STREAM_TLS_PORT \
		-e OME_WEBRTC_SIGNALLING_PORT \
		-e OME_WEBRTC_SIGNALLING_TLS_PORT \
		-e OME_WEBRTC_TCP_RELAY_PORT \
		-e OME_WEBRTC_CANDIDATE_PORT \
		--restart=unless-stopped \
		--mount=type=bind,source="${CONF_PATH}",target=/opt/ovenmediaengine/bin/origin_conf \
		--mount=type=bind,source="${LOGS_PATH}",target=/var/log/ovenmediaengine \
		--mount=type=bind,source="${CRASH_DUMPS_PATH}",target=/opt/ovenmediaengine/bin/dumps \
		--mount=type=bind,source="${OME_MEDIA_ROOT}",target=/opt/ovenmediaengine/media \
		--name "${CONTAINER_NAME}" "${IMAGE_NAME}" ||
	{
		loge "• ERROR: An error occurred while starting a container, stopping..."
		run "${DOCKER}" rm -f "${CONTAINER_NAME}"
		die
	}

	logi "• OvenMediaEngine is started successfully!"
}

_sh()
{
	die_if_container_not_running

	CONTAINER_ID=$(get_container_id)

	logi "• Run a shell in the running container: ID: ${PRESET_HIGHLIGHT}${CONTAINER_ID}"
	
	"${DOCKER}" exec -it \
	"${CONTAINER_ID}" \
	/bin/bash
}

_status()
{
	die_if_container_not_running
	CONTAINER_ID=$(get_container_id)

	logi "• Container is running: ID: ${PRESET_HIGHLIGHT}${CONTAINER_ID}${COLOR_RESET}, name: ${PRESET_HIGHLIGHT}${CONTAINER_NAME}"
}

_stop()
{
	die_if_container_not_running

	{
		logi "• Stopping a container: ${PRESET_HIGHLIGHT}${CONTAINER_NAME}"
		run "${DOCKER}" stop "${CONTAINER_NAME}" &&
		logi "• Removing a container: ${PRESET_HIGHLIGHT}${CONTAINER_NAME}"
		run "${DOCKER}" rm "${CONTAINER_NAME}"
	} ||
	{
		loge "• ERROR: An error occurred while stopping a container, force removing..."
		run "${DOCKER}" rm -f "${CONTAINER_NAME}"
		die
	}

	logi "• OvenMediaEngine is stopped successfully"
}

_restart()
{
	CONTAINER_ID=$(get_container_id)

	[ -z "${CONTAINER_ID}" ] && die "• ERROR: Container is not running"

	logi "• Restarting a container: ${PRESET_HIGHLIGHT}${CONTAINER_NAME}"
	run "${DOCKER}" restart "${CONTAINER_NAME}" ||
	die "• ERROR: An error occurred while restarting a container"
}

usage()
{
	case "${COMMAND}" in
		setup)
			logi "• Usage: ${PRESET_HIGHLIGHT}$0${COLOR_RESET} ${COLOR_MAGENTA}[OPTIONS]${COLOR_RESET} setup [SETUP_OPTIONS]"
			logi
			logi "• Setup directories for the container"
			logi
			logi "• Setup options:"
			logi "  ${COLOR_CYAN}-h${COLOR_RESET}, ${COLOR_CYAN}--help${COLOR_RESET}                        Show this help message and exit"
			;;

		start)
			logi "• Usage: ${PRESET_HIGHLIGHT}$0${COLOR_RESET} ${COLOR_MAGENTA}[OPTIONS]${COLOR_RESET} start [START_OPTIONS]"
			logi
			logi "• Start a docker container"
			logi
			logi "• Start options:"
			logi "  ${COLOR_CYAN}-h${COLOR_RESET}, ${COLOR_CYAN}--help${COLOR_RESET}                        Show this help message and exit"
			logi "  ${COLOR_CYAN}-i${COLOR_RESET}, ${COLOR_CYAN}--ip${COLOR_RESET} <IP address>             Specify the IPv4/IPv6 address to use for an ICE candidate"
			;;

		sh)
			logi "• Usage: ${PRESET_HIGHLIGHT}$0${COLOR_RESET} ${COLOR_MAGENTA}[OPTIONS]${COLOR_RESET} sh [SH_OPTIONS]"
			logi
			logi "• Launch a shell in the docker container"
			logi
			logi "• Shell options:"
			logi "  ${COLOR_CYAN}-h${COLOR_RESET}, ${COLOR_CYAN}--help${COLOR_RESET}                        Show this help message and exit"
			;;

		status)
			logi "• Usage: ${PRESET_HIGHLIGHT}$0${COLOR_RESET} ${COLOR_MAGENTA}[OPTIONS]${COLOR_RESET} status [STATUS_OPTIONS]"
			logi
			logi "• Show the status of the docker container"
			logi
			logi "• Status options:"
			logi "  ${COLOR_CYAN}-h${COLOR_RESET}, ${COLOR_CYAN}--help${COLOR_RESET}                        Show this help message and exit"
			;;

		stop)
			logi "• Usage: ${PRESET_HIGHLIGHT}$0${COLOR_RESET} ${COLOR_MAGENTA}[OPTIONS]${COLOR_RESET} stop [STOP_OPTIONS]"
			logi
			logi "• Stop the docker container"
			logi
			logi "• Stop options:"
			logi "  ${COLOR_CYAN}-h${COLOR_RESET}, ${COLOR_CYAN}--help${COLOR_RESET}                        Show this help message and exit"
			;;

		restart)
			logi "• Usage: ${PRESET_HIGHLIGHT}$0${COLOR_RESET} ${COLOR_MAGENTA}[OPTIONS]${COLOR_RESET} restart [RESTART_OPTIONS]"
			logi
			logi "• Restart the docker container"
			logi
			logi "• Restart options:"
			logi "  ${COLOR_CYAN}-h${COLOR_RESET}, ${COLOR_CYAN}--help${COLOR_RESET}                        Show this help message and exit"
			;;

		*)
			logi "• Usage: ${PRESET_HIGHLIGHT}$0${COLOR_RESET} ${COLOR_MAGENTA}[OPTIONS]${COLOR_RESET} ${COLOR_CYAN}COMMAND${COLOR_RESET} ..."
			logi
			logi "• Options:"
			logi "  ${COLOR_MAGENTA}-h${COLOR_RESET}, ${COLOR_MAGENTA}--help${COLOR_RESET}                        Show this help message and exit"
			logi "  ${COLOR_MAGENTA}-v${COLOR_RESET}, ${COLOR_MAGENTA}--version${COLOR_RESET}                     Show the version and exit"
			logi "  ${COLOR_MAGENTA}-d${COLOR_RESET}, ${COLOR_MAGENTA}--debug${COLOR_RESET}                       Show debug log"
			logi "  ${COLOR_MAGENTA}-b${COLOR_RESET}, ${COLOR_MAGENTA}--hide_banner${COLOR_RESET}                 Hide the banner"
			logi "  ${COLOR_MAGENTA}-m${COLOR_RESET}, ${COLOR_MAGENTA}--monochrome${COLOR_RESET}                  Disable colors"
			logi
			logi "• Commands:"
			logi "  ${COLOR_CYAN}setup${COLOR_RESET}       Download the latest Docker image and setup directories for the container"
			logi "  ${COLOR_CYAN}start${COLOR_RESET}       Start a docker container"
			logi "  ${COLOR_CYAN}sh${COLOR_RESET}          Run a shell in the docker container"
			logi "  ${COLOR_CYAN}status${COLOR_RESET}      Show the status of the docker container"
			logi "  ${COLOR_CYAN}stop${COLOR_RESET}        Stop the docker container"
			logi "  ${COLOR_CYAN}restart${COLOR_RESET}     Restart the docker container"
			;;
	esac

	exit 0
}

version()
{
	logi "• Launcher: ${COLOR_GREEN}${VERSION}"

	check_docker_is_available
	OME_VERSION=$("${DOCKER}" run \
			--rm \
			"${IMAGE_NAME}" \
			/bin/bash -c '/opt/ovenmediaengine/bin/OvenMediaEngine -v')

	logi "• OvenMediaEngine: ${COLOR_GREEN}${OME_VERSION}"

	exit 0
}

# Options
DEBUG=false
HIDE_BANNER=false
COMMAND=
parse_options()
{
	# Some options need to be processed before another option is parsed
	# Iterate all arguments without shift
	local USE_COLOR=true
	
	for ARG in "$@"
	do
		case "${ARG}" in
			-b|--hide_banner) HIDE_BANNER=true ;;
			-m|--monochrome) USE_COLOR=false ;;
		esac
	done

	${USE_COLOR} && prepare_colors
	banner

	# Parse global options
	while [ $# -gt 0 ]
	do
		case "$1" in
			-h|--help) usage ;;
			-v|--version) version ;;
			-d|--debug) DEBUG=true ;;
			-b|--hide_banner|-m|--monochrome) ;;
			-*) loge "• ERROR: Unknown option: $1"; logi; usage ;;
			*) break ;;
		esac
		shift
	done

	COMMAND="$1"
	shift

	# Parse common options
	case "$1" in
		-h|--help) usage ;;
	esac

	# Parse command specific options
	case "${COMMAND}" in
		start)
			while [ $# -gt 0 ]
			do
				case "$1" in
					-i|--ip)
						[ -z "$2" ] && loge "• ERROR: IP address must not be empty" && logi && usage
						IP_ADDRESS="$2"
						shift 2
						;;
					-*) loge "• ERROR: Unknown option: $1" && logi && usage ;;
				esac
			done
			;;

		setup|sh|status|stop|restart)
			# These commands don't have any options
			[ $# -gt 0 ] && loge "• ERROR: Unknown option: $1" && logi && usage
			;;

		'')
			loge '• ERROR: COMMAND must be specified'
			logi
			usage
			;;
		*)
			loge "• ERROR: Unknown command: ${COMMAND}" && logi && usage ;;
	esac
}
parse_options "${@}"

check_docker_is_available

COMMAND_FUNCTION="_${COMMAND}"

[ -z "${COMMAND}" ] && loge "• ERROR: COMMAND must be specified" && logi && usage
if [ ! "$(type -t "${COMMAND_FUNCTION}")" == 'function' ]
then
	loge "• Unknown command: ${COMMAND}"
	usage
fi

# Run the command
${COMMAND_FUNCTION}
