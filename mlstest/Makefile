# This is just a convenience Makefile to avoid having to remember
# all the CMake commands and their arguments.

# Set CMAKE_GENERATOR in the environment to select how you build, e.g.:
#   CMAKE_GENERATOR=Ninja

APP=mlstest
SOURCE_DIR=..
BUILD_DIR=../build
BIN=${BUILD_DIR}/${APP}/${APP}
CLANG_FORMAT=clang-format -i

.PHONY: all dev dev3 test ctest dtest dbtest libs test-libs test-all everything ci ci3 clean cclean format

${BIN}: ${BUILD_DIR} *.h *.cpp
	cmake --build ${BUILD_DIR}

${BUILD_DIR}: CMakeLists.txt
	cmake -B${BUILD_DIR} -S${SOURCE_DIR} -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DQUICR_BUILD_TESTS=ON

run: ${BIN}
	${BIN}

db: ${BIN}
	lldb ${BIN}

really:
	cd ../cmd/really && ../../build/cmd/really/really

clean:
	cmake --build ${BUILD_DIR} --target clean

cclean:
	rm -rf ${BUILD_DIR}

format:
	find . -iname "*.h" -or -iname "*.cpp" | xargs ${CLANG_FORMAT}
