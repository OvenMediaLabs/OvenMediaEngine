#
# Versions.cmake
# Single source of truth for all external dependency versions.
# Included by both InstallPrerequisites.cmake and Dependencies.cmake.
#

# These versions must stay in sync with what InstallPrerequisites.cmake builds.
# When upgrading a dependency, change the version here only.

# Format:
#   set(OME_VER_<NAME> <verify-version>)
#   set(OME_VER_<NAME> <verify-version>@<install-ref>)
#
# Examples:
#   set(OME_VER_SRT  1.5.2)
#   set(OME_VER_X264 0.164.x@31e19f92)
#
# The first part is always the version used for dependency verification.
# The optional "@<install-ref>" suffix overrides the source ref used for
# download/install.
#
# Note:
#   Some dependencies still fetch release assets instead of repository archives
#   even when an install-ref override is provided. For these entries, the
#   override value must follow the upstream release naming convention rather
#   than assuming an arbitrary commit hash/archive will work:
#     OME_VER_OPUS
#     OME_VER_JEMALLOC
#     OME_VER_PCRE2
#     OME_VER_WEBP
set(OME_VER_OPENSSL         3.0.7)
set(OME_VER_SRTP            2.4.2)
set(OME_VER_SRT             1.5.2)
set(OME_VER_OPUS            1.3.1)
set(OME_VER_VPX             1.11.0)
set(OME_VER_FDKAAC          2.0.2)
set(OME_VER_NASM            2.15.05)
set(OME_VER_FFMPEG          5.1.4)
set(OME_VER_JEMALLOC        5.3.0)
set(OME_VER_PCRE2           10.39)
set(OME_VER_OPENH264        2.4.0)
set(OME_VER_HIREDIS         1.0.2)
set(OME_VER_NVCC_HDR        11.1.5.2)
set(OME_VER_X264            0.164.x@31e19f92)
set(OME_VER_WEBP            1.5.0)
set(OME_VER_SPDLOG          1.15.1)
set(OME_VER_WHISPER         1.8.2)

# FFmpeg sub-library versions (from FFmpeg ${OME_VER_FFMPEG})
# These differ from the release version — obtained via pkg-config --modversion.
set(OME_VER_LIBAVFORMAT     59.27.100)
set(OME_VER_LIBAVFILTER     8.44.100)
set(OME_VER_LIBAVCODEC      59.37.100)
set(OME_VER_LIBSWRESAMPLE   4.7.100)
set(OME_VER_LIBSWSCALE      6.7.100)
set(OME_VER_LIBAVUTIL       57.28.100)
