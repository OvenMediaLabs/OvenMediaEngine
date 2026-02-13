#!/bin/bash

GIT_COMMAND="git"
GIT_INFO_FILE="git_info.h"
GIT_INFO_TEMPLATE_FILE="git_info.template"

SCRIPT_PATH=$(dirname $0)
SCRIPT_NAME=$(basename $0)

[ ! -z "${SCRIPT_PATH}" ] && cd "${SCRIPT_PATH}"

# Test git command is available
"${GIT_COMMAND}" --version >/dev/null 2>&1

GENERATE=0

GIT_VERSION=
GIT_VERSION_EXTRA=

if [ $? -eq 0 ]
then
	# Retrive git information for current branch
	GIT_VERSION=$("${GIT_COMMAND}" describe --tags --always 2>/dev/null)

	# Check previous file existance
	if [ ! -z "${GIT_VERSION}" ]
	then
		# Cloned from OME repository
		if [ ! -f "${GIT_INFO_FILE}" ]
		then
			[ -e "${GIT_INFO_FILE}" ] && echo "${GIT_INFO_FILE} already exists, and it isn't a regular file" && exit 1
		else
			# GIT_INFO_FILE is exists
			PREV_GIT_VERSION=$(cat "${GIT_INFO_FILE}" 2>/dev/null | grep OME_GIT_VERSION | grep -v OME_GIT_VERSION_EXTRA | awk ' { print $3 } ')
			PREV_GIT_VERSION=${PREV_GIT_VERSION#\"}
			PREV_GIT_VERSION=${PREV_GIT_VERSION%\"}

			[ "${PREV_GIT_VERSION}" != "${GIT_VERSION}" ] && echo "Information is changed (${PREV_GIT_VERSION} => ${GIT_VERSION})" && GENERATE=1
		fi

		GIT_VERSION_EXTRA=" (${GIT_VERSION})"
	else
		echo "This isn't a git repository"
		GIT_VERSION="(From archive)"
	fi
else
	echo "Could not execute git command"

	GIT_VERSION="(Unknown)"
fi

[ ! -f "${GIT_INFO_FILE}" ] && GENERATE=1

if [ ${GENERATE} -eq 1 ]
then
	echo "Trying to create ${GIT_INFO_FILE} from ${GIT_INFO_TEMPLATE_FILE}..."
	cp "${GIT_INFO_TEMPLATE_FILE}" "${GIT_INFO_FILE}"

	[ $? -ne 0 ] && echo "Could not create ${GIT_INFO_FILE}" && exit 1

	sed -i "s/__SCRIPT_NAME__/${SCRIPT_NAME}/" "${GIT_INFO_FILE}"
	sed -i "s/__GIT_VERSION__/\"${GIT_VERSION}\"/" "${GIT_INFO_FILE}"
	sed -i "s/__GIT_VERSION_EXTRA__/\"${GIT_VERSION_EXTRA}\"/" "${GIT_INFO_FILE}"
fi