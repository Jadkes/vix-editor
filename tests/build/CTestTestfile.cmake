# CMake generated Testfile for 
# Source directory: /home/jadkeskes/Documents/vix-editor/tests
# Build directory: /home/jadkeskes/Documents/vix-editor/tests/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[BufferTest]=] "/home/jadkeskes/Documents/vix-editor/tests/build/buffer_test")
set_tests_properties([=[BufferTest]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/jadkeskes/Documents/vix-editor/tests/CMakeLists.txt;46;add_test;/home/jadkeskes/Documents/vix-editor/tests/CMakeLists.txt;0;")
add_test([=[HistoryTest]=] "/home/jadkeskes/Documents/vix-editor/tests/build/history_test")
set_tests_properties([=[HistoryTest]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/jadkeskes/Documents/vix-editor/tests/CMakeLists.txt;47;add_test;/home/jadkeskes/Documents/vix-editor/tests/CMakeLists.txt;0;")
subdirs("_deps/googletest-build")
