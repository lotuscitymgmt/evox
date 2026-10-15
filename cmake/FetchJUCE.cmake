# JUCE 8 via FetchContent. To use an existing checkout instead of downloading:
#   cmake -B build -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
include(FetchContent)

set(EVOX_JUCE_TAG "8.0.15" CACHE STRING "JUCE git tag")

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${EVOX_JUCE_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)

FetchContent_MakeAvailable(JUCE)
