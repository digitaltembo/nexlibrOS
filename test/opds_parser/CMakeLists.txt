set(OPDS_SDK ${REPO_ROOT}/freeink-sdk/libs/network/Opds)

add_executable(OpdsParserTest
  OpdsParserTest.cpp
  ${OPDS_SDK}/src/OpdsParser.cpp
  ${OPDS_SDK}/src/Opds2Parser.cpp
  ${OPDS_SDK}/src/OpdsPublicationDoc.cpp
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/src/StreamingJsonParser.cpp

  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(OpdsParserTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${OPDS_SDK}/include
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/include
  ${REPO_ROOT}/lib/expat
)

target_compile_definitions(OpdsParserTest PRIVATE
  XML_CONTEXT_BYTES=1024
  XML_GE=0
  TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/"
)

target_link_libraries(OpdsParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(OpdsParserTest)
